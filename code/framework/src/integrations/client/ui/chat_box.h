/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

struct ImGuiInputTextCallbackData;

namespace Framework::Integrations::Client::UI {
    // In-game chat overlay (top-left): a fading backlog and a single-line input. Pure UI — the owner
    // supplies the send path via SetSubmitHandler and drives Render/OpenInput/session itself.
    class ChatBox {
      public:
        // author empty = notice line; color packed 0xRRGGBBAA on the body (0 = theme default).
        void AddMessage(const std::string &author, const std::string &text, uint32_t color = 0);

        // Invoked with the line when the user presses Enter.
        void SetSubmitHandler(std::function<void(const std::string &)> handler) {
            _submit = std::move(handler);
        }

        // Begin/stop typing. CloseInput(true) submits the current buffer.
        void OpenInput();
        void CloseInput(bool send);

        bool IsInputActive() const {
            return _inputActive;
        }

        void SetVisible(bool visible) {
            _visible = visible;
        }
        bool IsVisible() const {
            return _visible;
        }

        // Gate on the session lifecycle; clears the log + input when leaving.
        void SetSessionActive(bool active);
        bool IsSessionActive() const {
            return _sessionActive;
        }

        // Draw the overlay (call from inside the ImGui widget pass).
        void Render();

      private:
        struct Line {
            std::string author; // sender handle; empty = notice line
            std::string text;   // message body
            uint32_t color = 0; // packed 0xRRGGBBAA for the body; 0 = theme default
            std::string stamp;  // "HH:MM" wall-clock, shown dimmed
            double time = 0.0;  // ImGui::GetTime() when added, for idle fade-out
        };

        void Submit();

        // Up/Down recall of sent lines (ImGui CallbackHistory).
        static int TextEditCallbackStub(ImGuiInputTextCallbackData *data);
        int TextEditCallback(ImGuiInputTextCallbackData *data);

        std::function<void(const std::string &)> _submit;

        std::deque<Line> _messages;
        char _inputBuf[256]  = "";
        bool _inputActive    = false;
        bool _focusInput     = false;
        bool _scrollToBottom = false;
        bool _sessionActive  = false;
        bool _visible        = true;

        std::deque<std::string> _history; // sent lines, oldest at front
        int _historyPos = -1;             // -1: editing new line; else index into _history for recall

        static constexpr size_t kMaxMessages = 100;
        static constexpr size_t kMaxHistory  = 50;
    };
} // namespace Framework::Integrations::Client::UI
