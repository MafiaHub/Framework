/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <input/physical_key_state.h>
#include <input/window_input.h>

namespace InputTests {
    class WindowAdapter final: public Framework::Input::WindowInput {
      public:
        bool physicalProvider                    = false;
        bool stale                               = false;
        Framework::Input::KeyCodeSpace codeSpace = Framework::Input::KeyCodeSpace::PhysicalPosition;
        mutable int lastQuery                    = -1;
        bool ProvidesPhysicalKeyState() const override {
            return physicalProvider;
        }
        bool IsStateStale() const override {
            return stale;
        }
        Framework::Input::KeyCodeSpace GetKeyCodeSpace() const override {
            return codeSpace;
        }
        bool IsKeyDown(int key) const override {
            lastQuery = key;
            return Framework::Input::WindowInput::IsKeyDown(key);
        }
        void SetMousePosition(int, int) override {}
        void SetMouseVisible(bool) override {}
        bool IsMouseVisible() const override {
            return false;
        }
        void SetMouseLocked(bool) override {}
        bool IsMouseLocked() const override {
            return false;
        }
        void SetInputLocked(bool) override {}
        bool IsInputLocked() const override {
            return false;
        }
    };
} // namespace InputTests
MODULE(window_input, {
    IT("normalizes down and up even when the layout changes mid-hold", {
        InputTests::WindowAdapter input;
        input.ProcessEvent(nullptr, WM_KEYDOWN, 'Z', 0x11L << 16);
        EQUALS(input.IsKeyDown('W'), true);
        EQUALS(input.IsKeyDown('Z'), false);
        input.Update();
        input.ProcessEvent(nullptr, WM_KEYUP, 'X', (0x11L << 16) | (1L << 30));
        EQUALS(input.IsKeyReleased('W'), true);
        EQUALS(input.IsKeyDown('W'), false);
        EQUALS(static_cast<int>(input.GetKeyCodeSpace()), static_cast<int>(Framework::Input::KeyCodeSpace::PhysicalPosition));
        EQUALS(input.ProvidesPhysicalKeyState(), false);
    });
    IT("filters repeats including the first repeat after focus loss", {
        InputTests::WindowAdapter input;
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_F8, 0);
        EQUALS(input.IsKeyPressed(VK_F8), true);
        input.Update();
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_F8, 1L << 30);
        EQUALS(input.IsKeyPressed(VK_F8), false);
        input.ProcessEvent(nullptr, WM_KILLFOCUS, 0, 0);
        input.Update();
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_F8, 1L << 30);
        EQUALS(input.IsKeyDown(VK_F8), true);
        EQUALS(input.IsKeyPressed(VK_F8), false);
    });
    IT("mouse messages use button indices rather than modifier masks", {
        InputTests::WindowAdapter input;
        input.ProcessEvent(nullptr, WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT | MK_CONTROL, 0);
        EQUALS(input.IsMouseButtonDown(0), true);
        EQUALS(input.IsMouseButtonDown(1), false);
        EQUALS(input.IsKeyDown(VK_LBUTTON), true);
        input.ProcessEvent(nullptr, WM_LBUTTONUP, MK_SHIFT, 0);
        EQUALS(input.IsMouseButtonReleased(0), true);
        EQUALS(input.IsKeyDown(VK_LBUTTON), false);
        input.ProcessEvent(nullptr, WM_XBUTTONDOWN, static_cast<WPARAM>(XBUTTON2) << 16, 0);
        EQUALS(input.IsMouseButtonDown(4), true);
        EQUALS(input.IsKeyDown(VK_XBUTTON2), true);
    });
    IT("generic modifiers stay held until both sides are up", {
        InputTests::WindowAdapter input;
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_SHIFT, 0x2AL << 16);
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_SHIFT, 0x36L << 16);
        input.Update();
        input.ProcessEvent(nullptr, WM_KEYUP, VK_SHIFT, 0x2AL << 16);
        EQUALS(input.IsKeyDown(VK_LSHIFT), false);
        EQUALS(input.IsKeyDown(VK_RSHIFT), true);
        EQUALS(input.IsKeyDown(VK_SHIFT), true);
        EQUALS(input.IsKeyReleased(VK_SHIFT), false);
        input.ProcessEvent(nullptr, WM_KEYUP, VK_SHIFT, 0x36L << 16);
        EQUALS(input.IsKeyReleased(VK_SHIFT), true);
    });
    IT("focus loss releases every held key and mouse button and discards pending toggles", {
        InputTests::WindowAdapter input;
        input.ProcessEvent(nullptr, WM_KEYDOWN, 'W', 0x11L << 16);
        input.ProcessEvent(nullptr, WM_RBUTTONDOWN, MK_RBUTTON, 0);
        input.ProcessEvent(nullptr, WM_KILLFOCUS, 0, 0);
        EQUALS(input.IsKeyDown('W'), false);
        EQUALS(input.IsKeyPressed('W'), false);
        EQUALS(input.IsKeyReleased('W'), true);
        EQUALS(input.IsMouseButtonDown(1), false);
        EQUALS(input.IsMouseButtonPressed(1), false);
        EQUALS(input.IsMouseButtonReleased(1), true);
    });
    IT("client coordinates retain negative positions", {
        InputTests::WindowAdapter input;
        input.ProcessEvent(nullptr, WM_MOUSEMOVE, 0, static_cast<LPARAM>(static_cast<uint16_t>(-17)) | (static_cast<LPARAM>(static_cast<uint16_t>(-42)) << 16));
        int x = 0, y = 0;
        input.GetMousePosition(x, y);
        EQUALS(x, -17);
        EQUALS(y, -42);
    });
    IT("physical providers are queried without a second layout conversion", {
        InputTests::WindowAdapter input;
        input.physicalProvider = true;
        input.ProcessEvent(nullptr, WM_KEYDOWN, 'Z', 0x11L << 16);
        EQUALS(Framework::Input::PhysicalKeyState::IsDown('W', &input), true);
        EQUALS(input.lastQuery, 'W');
        input.stale = true;
        EQUALS(Framework::Input::PhysicalKeyState::IsDown('W', &input), false);
    });
    IT("legacy providers still receive virtual keys in their current layout", {
        InputTests::WindowAdapter input;
        input.physicalProvider = true;
        input.codeSpace        = Framework::Input::KeyCodeSpace::LayoutVirtualKey;
        Framework::Input::PhysicalKeyState::IsDown('W', &input);
        EQUALS(input.lastQuery, static_cast<int>(Framework::Input::PhysicalKeys::ToLayoutVirtualKey('W', GetKeyboardLayout(0))));
    });
});
