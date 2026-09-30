/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "button_state.h"
#include "win32_input.h"

namespace Framework::Input {
    // UI acquisition which stays live while a native game device is paused.
    class PollingInput: public Win32Input {
      public:
        void Update() override {
            _keys.Update(
                [this](int key) {
                    return ReadKeyDown(key);
                },
                IsAvailable());
        }
        bool IsKeyPressed(int key) const override {
            return IsAvailable() && _keys.IsPressed(key);
        }
        bool IsKeyReleased(int key) const override {
            return IsAvailable() && _keys.IsReleased(key);
        }

      private:
        KeySnapshot _keys;
    };
} // namespace Framework::Input
