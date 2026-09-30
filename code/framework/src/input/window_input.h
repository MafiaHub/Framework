/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "button_state.h"
#include "physical_keys.h"
#include "win32_input.h"

namespace Framework::Input {
    // State acquisition only. Cursor presentation and native control locking
    // remain on the game's adapter. This does not process WM_CHAR or IME.
    class WindowInput: public Win32Input {
      public:
        void Update() override {
            _keys.ClearEdges();
            _buttons.ClearEdges();
        }

        void GetMousePosition(int &x, int &y) const override {
            x = _mouseX;
            y = _mouseY;
        }

        bool IsKeyPressed(int key) const override {
            return IsAvailable() && _keys.IsPressed(key);
        }
        bool IsKeyReleased(int key) const override {
            return _keys.IsReleased(key);
        }
        bool IsMouseButtonPressed(int button) const override {
            return IsAvailable() && _buttons.IsPressed(button);
        }
        bool IsMouseButtonReleased(int button) const override {
            return _buttons.IsReleased(button);
        }

        void ProcessEvent(HWND, UINT message, WPARAM key, LPARAM flags) {
            switch (message) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYUP: {
                const bool down     = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
                const UINT physical = PhysicalKeys::FromKeyMessage(key, flags);
                // Retain the generic modifier while either side is held.
                UINT generic = physical;
                UINT side    = physical;
                if (physical == FW_KEY_SHIFT) {
                    side = ((flags >> 16) & 0xFF) == 0x36 ? FW_KEY_RSHIFT : FW_KEY_LSHIFT;
                }
                else if (physical == FW_KEY_CONTROL) {
                    side = (flags & (1L << 24)) != 0 ? FW_KEY_RCONTROL : FW_KEY_LCONTROL;
                }
                else if (physical == FW_KEY_MENU) {
                    side = (flags & (1L << 24)) != 0 ? FW_KEY_RMENU : FW_KEY_LMENU;
                }
                if (physical == FW_KEY_LSHIFT || physical == FW_KEY_RSHIFT)
                    generic = FW_KEY_SHIFT;
                else if (physical == FW_KEY_LCONTROL || physical == FW_KEY_RCONTROL)
                    generic = FW_KEY_CONTROL;
                else if (physical == FW_KEY_LMENU || physical == FW_KEY_RMENU)
                    generic = FW_KEY_MENU;
                const bool recordEdge = !down || (flags & (1L << 30)) == 0;
                _keys.Set(static_cast<int>(side), down, recordEdge);
                if (side != generic) {
                    _keys.Set(static_cast<int>(generic), _keys.IsDown(static_cast<int>(side)) || _keys.IsDown(static_cast<int>(side == FW_KEY_LSHIFT || side == FW_KEY_LCONTROL || side == FW_KEY_LMENU ? side + 1 : side - 1)), recordEdge);
                }
                break;
            }
            case WM_LBUTTONDOWN: SetButton(0, FW_KEY_LBUTTON, true); break;
            case WM_LBUTTONUP: SetButton(0, FW_KEY_LBUTTON, false); break;
            case WM_RBUTTONDOWN: SetButton(1, FW_KEY_RBUTTON, true); break;
            case WM_RBUTTONUP: SetButton(1, FW_KEY_RBUTTON, false); break;
            case WM_MBUTTONDOWN: SetButton(2, FW_KEY_MBUTTON, true); break;
            case WM_MBUTTONUP: SetButton(2, FW_KEY_MBUTTON, false); break;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP: {
                const bool first = HIWORD(key) == XBUTTON1;
                SetButton(first ? 3 : 4, first ? FW_KEY_XBUTTON1 : FW_KEY_XBUTTON2, message == WM_XBUTTONDOWN);
                break;
            }
            case WM_MOUSEMOVE:
                _mouseX = static_cast<short>(LOWORD(flags));
                _mouseY = static_cast<short>(HIWORD(flags));
                break;
            case WM_SETFOCUS:
                Update();
                for (int index = 0; index < 256; ++index) {
                    _keys.Set(index, IsKeyDown(index), false);
                }
                for (int index = 0; index < 5; ++index) {
                    _buttons.Set(index, IsMouseButtonDown(index), false);
                }
                break;
            case WM_KILLFOCUS:
                // Discard pending presses too: a focus transfer must not fire
                // a toggle which has not yet been consumed by the game tick.
                Update();
                _keys.ReleaseAll();
                _buttons.ReleaseAll();
                break;
            default: break;
            }
        }

      private:
        void SetButton(int index, int key, bool down) {
            _buttons.Set(index, down);
            _keys.Set(key, down);
        }
        ButtonState<256> _keys;
        ButtonState<5> _buttons;
        int _mouseX = 0;
        int _mouseY = 0;
    };
} // namespace Framework::Input
