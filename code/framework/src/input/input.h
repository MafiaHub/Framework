/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

#include "input_keymap.h"

namespace Framework::Input {
    class IInput {
      public:
        virtual ~IInput() = default;
        virtual void Update() = 0;

        virtual void SetMousePosition(int x, int y)   = 0;
        virtual void GetMousePosition(int &x, int &y) const = 0;
        virtual void SetMouseVisible(bool visible)          = 0;
        virtual bool IsMouseVisible() const                 = 0;
        virtual void SetMouseLocked(bool locked)            = 0;
        virtual bool IsMouseLocked() const                  = 0;
        virtual void SetInputLocked(bool locked)            = 0;
        virtual bool IsInputLocked() const                  = 0;

        // Opt-in: WndProc-fed modules can't answer a physical poll (no mouse VKs, no left/right
        // modifiers, and keys latch when a focus loss eats their WM_KEYUP).
        virtual bool ProvidesPhysicalKeyState() const {
            return false;
        }

        // Device state frozen rather than idle; callers edge-detecting must hold, not latch.
        virtual bool IsStateStale() const {
            return false;
        }

        virtual bool IsKeyDown(int key) const     = 0;
        virtual bool IsKeyUp(int key) const       = 0;
        virtual bool IsKeyPressed(int key) const  = 0;
        virtual bool IsKeyReleased(int key) const = 0;

        virtual bool IsMouseButtonDown(int button) const     = 0;
        virtual bool IsMouseButtonUp(int button) const       = 0;
        virtual bool IsMouseButtonPressed(int button) const  = 0;
        virtual bool IsMouseButtonReleased(int button) const = 0;

        virtual uint32_t MapKey(uint32_t key) const = 0;
    };
} // namespace Framework::Input
