/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "detail/win32_keys.h"
#include "input.h"

namespace Framework::Input {
    // Common OS acquisition and cursor operations for window and polling backends.
    // Games can override cursor/control-lock policy with their native operations.
    class Win32Input: public IInput {
      public:
        bool IsAvailable() const override {
            return HasFocus();
        }
        bool IsKeyDown(int key) const override {
            return ValidKey(key) && IsAvailable() && ReadKeyDown(key);
        }
        bool IsKeyUp(int key) const override {
            return ValidKey(key) && IsAvailable() && !ReadKeyDown(key);
        }
        bool IsMouseButtonDown(int button) const override {
            return ValidButton(button) && IsKeyDown(kMouseKeys[button]);
        }
        bool IsMouseButtonUp(int button) const override {
            return ValidButton(button) && IsKeyUp(kMouseKeys[button]);
        }
        bool IsMouseButtonPressed(int button) const override {
            return ValidButton(button) && IsKeyPressed(kMouseKeys[button]);
        }
        bool IsMouseButtonReleased(int button) const override {
            return ValidButton(button) && IsKeyReleased(kMouseKeys[button]);
        }
        void SetWindow(HWND window) {
            _window = window;
        }
        void GetMousePosition(int &x, int &y) const override {
            POINT point {};
            ::GetCursorPos(&point);
            if (_window)
                ::ScreenToClient(_window, &point);
            x = point.x;
            y = point.y;
        }
        void SetMousePosition(int x, int y) override {
            POINT point {x, y};
            if (_window)
                ::ClientToScreen(_window, &point);
            ::SetCursorPos(point.x, point.y);
        }
        void SetMouseVisible(bool visible) override {
            if (_visible == visible)
                return;
            _visible = visible;
            if (visible) {
                while (::ShowCursor(TRUE) < 0) {}
            }
            else {
                while (::ShowCursor(FALSE) >= 0) {}
            }
        }
        bool IsMouseVisible() const override {
            return _visible;
        }
        void SetMouseLocked(bool locked) override {
            _mouseLocked = locked;
            if (locked && _window) {
                RECT rect {};
                ::GetClientRect(_window, &rect);
                POINT first {rect.left, rect.top}, last {rect.right, rect.bottom};
                ::ClientToScreen(_window, &first);
                ::ClientToScreen(_window, &last);
                rect = {first.x, first.y, last.x, last.y};
                ::ClipCursor(&rect);
            }
            else {
                ::ClipCursor(nullptr);
            }
        }
        bool IsMouseLocked() const override {
            return _mouseLocked;
        }
        void SetInputLocked(bool locked) override {
            _inputLocked = locked;
        }
        bool IsInputLocked() const override {
            return _inputLocked;
        }

      protected:
        virtual bool HasFocus() const {
            return detail::IsForeground();
        }
        virtual bool ReadKeyDown(int key) const {
            return detail::ReadWin32Key(key);
        }

      private:
        static bool ValidKey(int key) {
            return key >= 0 && key < 256;
        }
        static bool ValidButton(int button) {
            return button >= 0 && button < 5;
        }
        static constexpr int kMouseKeys[] {FW_KEY_LBUTTON, FW_KEY_RBUTTON, FW_KEY_MBUTTON, FW_KEY_XBUTTON1, FW_KEY_XBUTTON2};
        HWND _window      = nullptr;
        bool _visible     = true;
        bool _mouseLocked = false;
        bool _inputLocked = false;
    };
} // namespace Framework::Input
