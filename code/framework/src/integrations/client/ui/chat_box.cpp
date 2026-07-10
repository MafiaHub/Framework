/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "chat_box.h"

#include <imgui/imgui.h>

#include <cstdio>
#include <ctime>

namespace Framework::Integrations::Client::UI {
    namespace {
        // Idle lines stay solid for kFadeDelay seconds, then fade out over kFadeFade seconds.
        constexpr double kFadeDelay = 12.0;
        constexpr double kFadeFade  = 2.0;

        constexpr float kWidth  = 440.0f;
        constexpr float kHeight = 264.0f;
        constexpr float kMargin = 18.0f;

        const ImVec4 kColAccent = ImVec4(0.36f, 0.66f, 1.00f, 1.0f); // sender handle
        const ImVec4 kColText   = ImVec4(0.88f, 0.89f, 0.93f, 1.0f); // message body
        const ImVec4 kColNotice = ImVec4(0.98f, 0.84f, 0.45f, 1.0f); // server/system line
        const ImVec4 kColStamp  = ImVec4(1.00f, 1.00f, 1.00f, 0.34f); // timestamp

        ImVec4 WithAlpha(ImVec4 c, float a) {
            c.w *= a;
            return c;
        }

        // Unpack 0xRRGGBBAA; a zero alpha byte reads as opaque so a bare 0xRRGGBB still shows.
        ImVec4 FromPacked(uint32_t c) {
            const float r     = static_cast<float>((c >> 24) & 0xFF) / 255.0f;
            const float g     = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
            const float b     = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
            const uint32_t a8 = c & 0xFF;
            const float a     = (a8 == 0) ? 1.0f : static_cast<float>(a8) / 255.0f;
            return ImVec4(r, g, b, a);
        }

        // "HH:MM  name: body" (stamp dim, name accented, body in color or theme); empty author = a
        // notice line. alpha drives the idle fade.
        void DrawLine(const std::string &author, const std::string &text, uint32_t color, const std::string &stamp, float alpha) {
            if (!stamp.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(kColStamp, alpha));
                ImGui::TextUnformatted(stamp.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 8.0f);
            }

            if (!author.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(kColAccent, alpha));
                ImGui::TextUnformatted(author.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(kColText, alpha));
                ImGui::TextUnformatted(": ");
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 0.0f);
                const ImVec4 body = (color != 0) ? FromPacked(color) : kColText;
                ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(body, alpha));
                ImGui::TextWrapped("%s", text.c_str());
                ImGui::PopStyleColor();
            }
            else {
                const ImVec4 body = (color != 0) ? FromPacked(color) : kColNotice;
                ImGui::PushStyleColor(ImGuiCol_Text, WithAlpha(body, alpha));
                ImGui::TextWrapped("%s", text.c_str());
                ImGui::PopStyleColor();
            }
        }
    } // namespace

    void ChatBox::AddMessage(const std::string &author, const std::string &text, uint32_t color) {
        if (text.empty()) {
            return;
        }
        char stamp[8] = "";
        std::time_t t = std::time(nullptr);
        std::tm lt {};
        if (localtime_s(&lt, &t) == 0) {
            std::snprintf(stamp, sizeof(stamp), "%02d:%02d", lt.tm_hour, lt.tm_min);
        }
        _messages.push_back({author, text, color, stamp, ImGui::GetTime()});
        while (_messages.size() > kMaxMessages) {
            _messages.pop_front();
        }
        _scrollToBottom = true;
    }

    void ChatBox::OpenInput() {
        if (!_sessionActive || _inputActive) {
            return;
        }
        _inputActive    = true;
        _focusInput     = true;
        _inputBuf[0]    = '\0';
        _historyPos     = -1;
        _scrollToBottom = true;
    }

    void ChatBox::CloseInput(bool send) {
        if (send) {
            Submit();
        }
        _inputActive = false;
        _inputBuf[0] = '\0';
    }

    void ChatBox::Submit() {
        std::string text = _inputBuf;
        const auto first = text.find_first_not_of(" \t");
        if (first == std::string::npos) {
            return;
        }
        const auto last = text.find_last_not_of(" \t");
        text            = text.substr(first, last - first + 1);
        if (text.empty()) {
            return;
        }
        // Record for recall, skipping an immediate repeat.
        if (_history.empty() || _history.back() != text) {
            _history.push_back(text);
            while (_history.size() > kMaxHistory) {
                _history.pop_front();
            }
        }
        if (_submit) {
            _submit(text);
        }
    }

    int ChatBox::TextEditCallbackStub(ImGuiInputTextCallbackData *data) {
        return static_cast<ChatBox *>(data->UserData)->TextEditCallback(data);
    }

    int ChatBox::TextEditCallback(ImGuiInputTextCallbackData *data) {
        if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || _history.empty()) {
            return 0;
        }
        const int prev = _historyPos;
        if (data->EventKey == ImGuiKey_UpArrow) {
            if (_historyPos == -1) {
                _historyPos = static_cast<int>(_history.size()) - 1;
            }
            else if (_historyPos > 0) {
                _historyPos--;
            }
        }
        else if (data->EventKey == ImGuiKey_DownArrow) {
            if (_historyPos != -1 && ++_historyPos >= static_cast<int>(_history.size())) {
                _historyPos = -1; // past newest -> empty line
            }
        }
        if (prev != _historyPos) {
            const char *replace = (_historyPos >= 0) ? _history[_historyPos].c_str() : "";
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, replace);
        }
        return 0;
    }

    void ChatBox::SetSessionActive(bool active) {
        _sessionActive = active;
        if (!active) {
            _inputActive = false;
            _inputBuf[0] = '\0';
            _messages.clear();
        }
    }

    void ChatBox::Render() {
        if (!_sessionActive) {
            return;
        }
        const bool typing = _inputActive;
        const double now  = ImGui::GetTime();

        // When idle, suppress the whole window once every line has faded.
        if (!typing) {
            bool anyVisible = false;
            for (const auto &m : _messages) {
                if (now - m.time < kFadeDelay + kFadeFade) {
                    anyVisible = true;
                    break;
                }
            }
            if (!anyVisible) {
                return;
            }
        }

        const float panelAlpha = typing ? 0.94f : 0.46f;

        ImGui::SetNextWindowPos(ImVec2(kMargin, kMargin), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 9.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 4.0f);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
        if (!typing) {
            flags |= ImGuiWindowFlags_NoInputs; // log-only: clicks pass through to the game
        }

        if (ImGui::Begin("##fw_chat", nullptr, flags)) {
            // Rounded translucent slab with a soft border and an accent left edge, drawn first so it
            // sits behind the text. (Default window bg is disabled above.)
            ImDrawList *dl  = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetWindowPos();
            const ImVec2 p1 = ImVec2(p0.x + ImGui::GetWindowSize().x, p0.y + ImGui::GetWindowSize().y);
            const int a8    = static_cast<int>(panelAlpha * 255.0f);
            dl->AddRectFilled(p0, p1, IM_COL32(18, 19, 26, a8), 8.0f);
            dl->AddRect(p0, p1, IM_COL32(255, 255, 255, typing ? 40 : 18), 8.0f, 0, 1.0f);
            dl->AddRectFilled(p0, ImVec2(p0.x + 3.0f, p1.y), IM_COL32(92, 168, 255, a8), 8.0f, ImDrawFlags_RoundCornersLeft);

            const float footer = typing ? (ImGui::GetTextLineHeight() + 12.0f + ImGui::GetStyle().ItemSpacing.y * 2.0f) : 0.0f;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(255, 255, 255, 38));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(255, 255, 255, 70));
            ImGui::BeginChild("##fw_chat_log", ImVec2(0.0f, -footer), false, ImGuiWindowFlags_NoSavedSettings);
            for (const auto &m : _messages) {
                float alpha = 1.0f;
                if (!typing) {
                    const double age = now - m.time;
                    if (age > kFadeDelay + kFadeFade) {
                        continue;
                    }
                    if (age > kFadeDelay) {
                        alpha = 1.0f - static_cast<float>((age - kFadeDelay) / kFadeFade);
                    }
                }
                DrawLine(m.author, m.text, m.color, m.stamp, alpha);
            }
            // Auto-scroll: snap on new lines, and keep pinned while already at the bottom.
            if (_scrollToBottom || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
                _scrollToBottom = false;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(4);

            if (typing) {
                ImGui::Spacing();
                if (_focusInput) {
                    ImGui::SetKeyboardFocusHere();
                    _focusInput = false;
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f, 6.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 16));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 26));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 30));
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(235, 237, 242, 255));
                ImGui::PushItemWidth(-1.0f);
                const ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
                if (ImGui::InputTextWithHint("##fw_chat_input", "Press Enter to send, Up/Down for history, Esc to cancel", _inputBuf, sizeof(_inputBuf), inputFlags, &ChatBox::TextEditCallbackStub, this)) {
                    CloseInput(true);
                }
                ImGui::PopItemWidth();
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar(2);
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    CloseInput(false);
                }
            }
        }
        ImGui::End();

        ImGui::PopStyleVar(5);
    }
} // namespace Framework::Integrations::Client::UI
