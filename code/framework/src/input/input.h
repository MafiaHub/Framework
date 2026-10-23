/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "input_keymap.h"

#include <cstdint>

namespace Framework::Input {
    inline bool IsWheelKey(int key) {
        return key == FW_KEY_WHEELUP || key == FW_KEY_WHEELDOWN;
    }

    class IInput {
      public:
        virtual ~IInput()     = default;
        virtual void Update() = 0;

        virtual void SetMousePosition(int x, int y)         = 0;
        virtual void GetMousePosition(int &x, int &y) const = 0;
        virtual void SetMouseVisible(bool visible)          = 0;
        virtual bool IsMouseVisible() const                 = 0;
        virtual void SetMouseLocked(bool locked)            = 0;
        virtual bool IsMouseLocked() const                  = 0;
        virtual void SetInputLocked(bool locked)            = 0;
        virtual bool IsInputLocked() const                  = 0;

        // Fresh source state is available. Edge detectors reseed after an unavailable period.
        virtual bool IsAvailable() const = 0;

        // Every adapter accepts FW_KEY_* codes and US physical letter/digit positions.
        // Native layout and scan-code conversion belongs inside the adapter.
        virtual bool IsKeyDown(int key) const     = 0;
        virtual bool IsKeyUp(int key) const       = 0;
        virtual bool IsKeyPressed(int key) const  = 0;
        virtual bool IsKeyReleased(int key) const = 0;

        virtual bool IsMouseButtonDown(int button) const     = 0;
        virtual bool IsMouseButtonUp(int button) const       = 0;
        virtual bool IsMouseButtonPressed(int button) const  = 0;
        virtual bool IsMouseButtonReleased(int button) const = 0;

        // Wheel notches are numbered in arrival order and never renumbered. A notch has no held state,
        // so a consumer keeps the last number it handled and reads the ones after it.
        static constexpr uint32_t kWheelNotchHistory = 64;
        virtual uint32_t GetLastWheelNotch() const {
            return 0;
        }
        // FW_KEY_WHEELUP or FW_KEY_WHEELDOWN; 0 once the notch is discarded by focus loss or is older
        // than the last kWheelNotchHistory. An adapter that hides keys from IsKeyDown should hide these too.
        virtual int GetWheelNotch(uint32_t /*number*/) const {
            return 0;
        }
    };
} // namespace Framework::Input
