/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "unit.h"
#include <integrations/client/ui/chat_box.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>

namespace {
    struct ChatFixture {
        Framework::Integrations::Client::UI::ChatBox chat;

        ChatFixture(int messages = 40) {
            ImGui::CreateContext();
            auto &io       = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.DisplaySize = ImVec2(1280.0f, 720.0f);
            io.DeltaTime   = 1.0f / 60.0f;
            unsigned char *pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            chat.SetSessionActive(true);
            for (int i = 0; i < messages; ++i) {
                chat.AddMessage("Player", "Message " + std::to_string(i));
            }
            chat.OpenInput();
            Frame(4);
        }

        ~ChatFixture() {
            ImGui::DestroyContext();
        }

        void Frame(int count = 1) {
            for (int i = 0; i < count; ++i) {
                ImGui::NewFrame();
                chat.Render();
                ImGui::Render();
            }
        }

        ImGuiWindow *Log() const {
            for (auto *window : ImGui::GetCurrentContext()->Windows) {
                if (window->ParentWindow && strstr(window->Name, "##fw_chat_log")) {
                    return window;
                }
            }
            return nullptr;
        }

        float Scroll() const {
            return Log()->Scroll.y;
        }

        bool AtBottom() const {
            return std::abs(Scroll() - Log()->ScrollMax.y) < 1.0f;
        }

        void Key(ImGuiKey key) {
            ImGui::GetIO().AddKeyEvent(key, true);
            Frame();
            ImGui::GetIO().AddKeyEvent(key, false);
            Frame(2);
        }

        void Wheel(float x, float y, float delta) {
            ImGui::GetIO().AddMousePosEvent(x, y);
            Frame();
            ImGui::GetIO().AddMouseWheelEvent(0.0f, delta);
            Frame(2);
        }
    };
} // namespace

// clang-format off
MODULE(chat_scroll, {
    IT("Page Up scrolls one viewport while retaining text input focus", {
        ChatFixture f;
        EQUALS(f.AtBottom(), true);
        const auto active = ImGui::GetActiveID();
        const float before = f.Scroll();
        f.Key(ImGuiKey_PageUp);
        GREATER(before - f.Scroll(), 100.0f);
        LESSEREQ(before - f.Scroll(), f.Log()->Size.y);
        EQUALS(ImGui::GetActiveID(), active);
        EQUALS(f.chat.IsInputActive(), true);
        f.Key(ImGuiKey_PageDown);
        EQUALS(f.AtBottom(), true);
    });

    IT("held Page Up repeats and both ends clamp", {
        ChatFixture f;
        ImGui::GetIO().AddKeyEvent(ImGuiKey_PageUp, true);
        f.Frame(120);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_PageUp, false);
        f.Frame(2);
        EQUALS(f.Scroll(), 0.0f);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_PageDown, true);
        f.Frame(120);
        ImGui::GetIO().AddKeyEvent(ImGuiKey_PageDown, false);
        f.Frame(2);
        EQUALS(f.AtBottom(), true);
    });

    IT("wheel scrolls over the log with the input still active", {
        ChatFixture f;
        const auto active = ImGui::GetActiveID();
        const float before = f.Scroll();
        f.Wheel(100.0f, 100.0f, 1.0f);
        LESSER(f.Scroll(), before);
        EQUALS(ImGui::GetActiveID(), active);
        f.Wheel(100.0f, 100.0f, -1.0f);
        EQUALS(f.AtBottom(), true);
    });

    IT("wheel also scrolls over the chat input and panel padding", {
        ChatFixture f;
        float before = f.Scroll();
        f.Wheel(100.0f, 260.0f, 1.0f);
        LESSER(f.Scroll(), before);
        before = f.Scroll();
        f.Wheel(20.0f, 20.0f, 1.0f);
        LESSER(f.Scroll(), before);
    });

    IT("wheel outside chat leaves its scroll position unchanged", {
        ChatFixture f;
        const float before = f.Scroll();
        f.Wheel(700.0f, 400.0f, 1.0f);
        EQUALS(f.Scroll(), before);
    });

    IT("new messages preserve the position while reading history", {
        ChatFixture f;
        f.Key(ImGuiKey_PageUp);
        const float before = f.Scroll();
        f.chat.AddMessage("Player", "An incoming message while reading");
        f.Frame(3);
        EQUALS(f.Scroll(), before);
        EQUALS(f.AtBottom(), false);
    });

    IT("new messages follow once scrolled back to the bottom", {
        ChatFixture f;
        f.Key(ImGuiKey_PageUp);
        f.Key(ImGuiKey_PageDown);
        const float before = f.Scroll();
        f.chat.AddMessage("Player", "An incoming message at the bottom");
        f.Frame(3);
        EQUALS(f.AtBottom(), true);
        GREATER(f.Scroll(), before);
    });

    IT("evicting old messages preserves the visible text, including wrapped lines", {
        ChatFixture f(99);
        const float contentAt99 = f.Log()->ContentSize.y;
        f.chat.AddMessage("Player", "The hundredth line");
        f.Frame(3);
        const float contentBefore = f.Log()->ContentSize.y;
        const float lineHeight = contentBefore - contentAt99;
        f.chat.AddMessage("Player", std::string(200, 'W'));
        f.Frame(3);
        const float wrappedHeight = f.Log()->ContentSize.y - contentBefore + lineHeight;
        for (int i = 0; i < 99; ++i) {
            f.chat.AddMessage("Player", "Another line");
        }
        f.Frame(3);
        f.Key(ImGuiKey_PageUp);
        const float before = f.Scroll();
        f.chat.AddMessage("Player", "Evicts the first wrapped message");
        f.Frame(3);
        LESSER(std::abs(f.Scroll() - (before - wrappedHeight)), 1.0f);
        EQUALS(f.AtBottom(), false);
    });

    IT("scrolling up wins when a message arrives on the same frame", {
        ChatFixture f;
        const float before = f.Scroll();
        f.chat.AddMessage("Player", "Arrives as Page Up is pressed");
        f.Key(ImGuiKey_PageUp);
        LESSER(f.Scroll(), before);
        EQUALS(f.AtBottom(), false);
    });

    IT("closing and reopening returns to recent messages", {
        ChatFixture f;
        f.Key(ImGuiKey_PageUp);
        f.chat.CloseInput(false);
        f.Frame(3);
        EQUALS(f.AtBottom(), true);
        f.chat.OpenInput();
        f.Frame(4);
        EQUALS(f.AtBottom(), true);
    });

    IT("empty and short logs stay at zero", {
        ChatFixture f(0);
        f.Key(ImGuiKey_PageUp);
        f.Wheel(100.0f, 100.0f, -1.0f);
        EQUALS(f.Scroll(), 0.0f);
        f.chat.AddMessage("", "Only one line");
        f.Frame(3);
        f.Key(ImGuiKey_PageDown);
        EQUALS(f.Scroll(), 0.0f);
    });

    IT("closed chat ignores scrolling and a new session starts at the bottom", {
        ChatFixture f;
        f.chat.CloseInput(false);
        f.Frame(3);
        f.Key(ImGuiKey_PageUp);
        f.Wheel(100.0f, 100.0f, 1.0f);
        EQUALS(f.AtBottom(), true);
        f.chat.SetSessionActive(false);
        f.Frame();
        f.chat.SetSessionActive(true);
        f.chat.AddMessage("", "New session");
        f.chat.OpenInput();
        f.Frame(4);
        EQUALS(f.Scroll(), 0.0f);
    });
});
// clang-format on

int main() {
    UNIT_CREATE("FrameworkChatTests");
    UNIT_MODULE(chat_scroll);
    return UNIT_RUN();
}
