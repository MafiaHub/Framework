/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "button_state.h"
#include "input.h"
#include "physical_keys.h"

namespace Framework::Input {
    // State acquisition only. Cursor presentation and native control locking
    // remain on the game's adapter. This does not process WM_CHAR or IME.
    class WindowInput: public IInput {
      public:
        void Update() override {
            _keys.ClearEdges();
            _buttons.ClearEdges();
        }

        void GetMousePosition(int &x, int &y) const override {
            x = _mouseX;
            y = _mouseY;
        }

        uint32_t MapKey(uint32_t key) const override {
            return key;
        }
        KeyCodeSpace GetKeyCodeSpace() const override {
            return KeyCodeSpace::PhysicalPosition;
        }
        bool IsKeyDown(int key) const override {
            return _keys.IsDown(key);
        }
        bool IsKeyUp(int key) const override {
            return _keys.IsUp(key);
        }
        bool IsKeyPressed(int key) const override {
            return _keys.IsPressed(key);
        }
        bool IsKeyReleased(int key) const override {
            return _keys.IsReleased(key);
        }
        bool IsMouseButtonDown(int button) const override {
            return _buttons.IsDown(button);
        }
        bool IsMouseButtonUp(int button) const override {
            return _buttons.IsUp(button);
        }
        bool IsMouseButtonPressed(int button) const override {
            return _buttons.IsPressed(button);
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
                if (physical == VK_SHIFT) {
                    side = ((flags >> 16) & 0xFF) == 0x36 ? VK_RSHIFT : VK_LSHIFT;
                }
                else if (physical == VK_CONTROL) {
                    side = (flags & (1L << 24)) != 0 ? VK_RCONTROL : VK_LCONTROL;
                }
                else if (physical == VK_MENU) {
                    side = (flags & (1L << 24)) != 0 ? VK_RMENU : VK_LMENU;
                }
                if (physical == VK_LSHIFT || physical == VK_RSHIFT)
                    generic = VK_SHIFT;
                else if (physical == VK_LCONTROL || physical == VK_RCONTROL)
                    generic = VK_CONTROL;
                else if (physical == VK_LMENU || physical == VK_RMENU)
                    generic = VK_MENU;
                const bool recordEdge = !down || (flags & (1L << 30)) == 0;
                _keys.Set(static_cast<int>(side), down, recordEdge);
                if (side != generic) {
                    _keys.Set(static_cast<int>(generic), _keys.IsDown(static_cast<int>(side)) || _keys.IsDown(static_cast<int>(side == VK_LSHIFT || side == VK_LCONTROL || side == VK_LMENU ? side + 1 : side - 1)), recordEdge);
                }
                break;
            }
            case WM_LBUTTONDOWN: SetButton(0, VK_LBUTTON, true); break;
            case WM_LBUTTONUP: SetButton(0, VK_LBUTTON, false); break;
            case WM_RBUTTONDOWN: SetButton(1, VK_RBUTTON, true); break;
            case WM_RBUTTONUP: SetButton(1, VK_RBUTTON, false); break;
            case WM_MBUTTONDOWN: SetButton(2, VK_MBUTTON, true); break;
            case WM_MBUTTONUP: SetButton(2, VK_MBUTTON, false); break;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP: {
                const bool first = HIWORD(key) == XBUTTON1;
                SetButton(first ? 3 : 4, first ? VK_XBUTTON1 : VK_XBUTTON2, message == WM_XBUTTONDOWN);
                break;
            }
            case WM_MOUSEMOVE:
                _mouseX = static_cast<short>(LOWORD(flags));
                _mouseY = static_cast<short>(HIWORD(flags));
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
