/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <input/window_input.h>

namespace InputTests {
    class WindowAdapter final: public Framework::Input::WindowInput {
      public:
        std::array<bool, 256> held {};
        bool foreground = true;

      protected:
        bool HasFocus() const override {
            return foreground;
        }
        bool ReadKeyDown(int key) const override {
            return held[key];
        }

      public:
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
        input.held['W'] = true;
        input.ProcessEvent(nullptr, WM_KEYDOWN, 'Z', 0x11L << 16);
        EQUALS(input.IsKeyDown('W'), true);
        EQUALS(input.IsKeyDown('Z'), false);
        input.Update();
        input.held['W'] = false;
        input.ProcessEvent(nullptr, WM_KEYUP, 'X', (0x11L << 16) | (1L << 30));
        EQUALS(input.IsKeyReleased('W'), true);
        EQUALS(input.IsKeyDown('W'), false);
    });
    IT("filters repeats including the first repeat after focus loss", {
        InputTests::WindowAdapter input;
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_F8, 0);
        EQUALS(input.IsKeyPressed(FW_KEY_F8), true);
        input.Update();
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_F8, 1L << 30);
        EQUALS(input.IsKeyPressed(FW_KEY_F8), false);
        input.ProcessEvent(nullptr, WM_KILLFOCUS, 0, 0);
        input.Update();
        input.held[FW_KEY_F8] = true;
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_F8, 1L << 30);
        EQUALS(input.IsKeyDown(FW_KEY_F8), true);
        EQUALS(input.IsKeyPressed(FW_KEY_F8), false);
    });
    IT("mouse messages use button indices rather than modifier masks", {
        InputTests::WindowAdapter input;
        input.held[FW_KEY_LBUTTON] = true;
        input.ProcessEvent(nullptr, WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT | MK_CONTROL, 0);
        EQUALS(input.IsMouseButtonDown(0), true);
        EQUALS(input.IsMouseButtonDown(1), false);
        EQUALS(input.IsKeyDown(FW_KEY_LBUTTON), true);
        input.held[FW_KEY_LBUTTON] = false;
        input.ProcessEvent(nullptr, WM_LBUTTONUP, MK_SHIFT, 0);
        EQUALS(input.IsMouseButtonReleased(0), true);
        EQUALS(input.IsKeyDown(FW_KEY_LBUTTON), false);
        input.held[FW_KEY_XBUTTON2] = true;
        input.ProcessEvent(nullptr, WM_XBUTTONDOWN, static_cast<WPARAM>(XBUTTON2) << 16, 0);
        EQUALS(input.IsMouseButtonDown(4), true);
        EQUALS(input.IsKeyDown(FW_KEY_XBUTTON2), true);
    });
    IT("generic modifiers stay held until both sides are up", {
        InputTests::WindowAdapter input;
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_SHIFT, 0x2AL << 16);
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_SHIFT, 0x36L << 16);
        input.Update();
        input.held[FW_KEY_RSHIFT] = true;
        input.held[FW_KEY_SHIFT]  = true;
        input.ProcessEvent(nullptr, WM_KEYUP, VK_SHIFT, 0x2AL << 16);
        EQUALS(input.IsKeyDown(FW_KEY_LSHIFT), false);
        EQUALS(input.IsKeyDown(FW_KEY_RSHIFT), true);
        EQUALS(input.IsKeyDown(FW_KEY_SHIFT), true);
        EQUALS(input.IsKeyReleased(FW_KEY_SHIFT), false);
        input.held[FW_KEY_RSHIFT] = false;
        input.held[FW_KEY_SHIFT]  = false;
        input.ProcessEvent(nullptr, WM_KEYUP, VK_SHIFT, 0x36L << 16);
        EQUALS(input.IsKeyReleased(FW_KEY_SHIFT), true);
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
    IT("held queries work before a window message and are scoped to foreground", {
        InputTests::WindowAdapter input;
        input.held[FW_KEY_SPACE] = true;
        EQUALS(input.IsKeyDown(FW_KEY_SPACE), true);
        EQUALS(input.IsKeyPressed(FW_KEY_SPACE), false);
        input.foreground = false;
        EQUALS(input.IsAvailable(), false);
        EQUALS(input.IsKeyDown(FW_KEY_SPACE), false);
        EQUALS(input.IsKeyUp(FW_KEY_SPACE), false);
        input.foreground = true;
        EQUALS(input.IsKeyDown(FW_KEY_SPACE), true);
        EQUALS(input.IsKeyPressed(FW_KEY_SPACE), false);
    });
    IT("the common query API rejects invalid keys and mouse button indices", {
        InputTests::WindowAdapter input;
        for (int key : {-1, 256}) {
            EQUALS(input.IsKeyDown(key), false);
            EQUALS(input.IsKeyUp(key), false);
            EQUALS(input.IsKeyPressed(key), false);
            EQUALS(input.IsKeyReleased(key), false);
        }
        for (int button : {-1, 5}) {
            EQUALS(input.IsMouseButtonDown(button), false);
            EQUALS(input.IsMouseButtonUp(button), false);
            EQUALS(input.IsMouseButtonPressed(button), false);
            EQUALS(input.IsMouseButtonReleased(button), false);
        }
    });
    IT("focus seeds already held modifiers before a second side is pressed", {
        InputTests::WindowAdapter input;
        input.held[FW_KEY_LSHIFT] = true;
        input.held[FW_KEY_SHIFT]  = true;
        input.ProcessEvent(nullptr, WM_SETFOCUS, 0, 0);
        EQUALS(input.IsKeyPressed(FW_KEY_SHIFT), false);
        input.held[FW_KEY_RSHIFT] = true;
        input.ProcessEvent(nullptr, WM_KEYDOWN, VK_SHIFT, 0x36L << 16);
        EQUALS(input.IsKeyPressed(FW_KEY_RSHIFT), true);
        EQUALS(input.IsKeyPressed(FW_KEY_SHIFT), false);
        input.ProcessEvent(nullptr, WM_KEYUP, VK_SHIFT, 0x36L << 16);
        EQUALS(input.IsKeyReleased(FW_KEY_SHIFT), false);
    });
});
