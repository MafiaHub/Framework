/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <input/polling_input.h>

namespace InputTests {
    class PollingAdapter final: public Framework::Input::PollingInput {
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
    };
} // namespace InputTests

MODULE(polling_input, {
    IT("uses the IInput query API for live held state and independent frame edges", {
        InputTests::PollingAdapter source;
        Framework::Input::IInput &input = source;
        input.Update();
        source.held[FW_KEY_SPACE] = true;
        EQUALS(input.IsKeyDown(FW_KEY_SPACE), true);
        input.Update();
        EQUALS(input.IsKeyPressed(FW_KEY_SPACE), true);
        EQUALS(input.IsKeyPressed(FW_KEY_SPACE), true);
        input.Update();
        EQUALS(input.IsKeyPressed(FW_KEY_SPACE), false);
        source.held[FW_KEY_SPACE] = false;
        input.Update();
        EQUALS(input.IsKeyUp(FW_KEY_SPACE), true);
        EQUALS(input.IsKeyReleased(FW_KEY_SPACE), true);
    });
    IT("focus reacquisition seeds held state without a new press", {
        InputTests::PollingAdapter input;
        input.held['W'] = true;
        input.Update();
        EQUALS(input.IsKeyDown('W'), true);
        EQUALS(input.IsKeyPressed('W'), false);
        input.foreground = false;
        input.Update();
        EQUALS(input.IsKeyDown('W'), false);
        EQUALS(input.IsKeyUp('W'), false);
        EQUALS(input.IsKeyReleased('W'), false);
        input.foreground = true;
        input.Update();
        EQUALS(input.IsKeyDown('W'), true);
        EQUALS(input.IsKeyPressed('W'), false);
    });
    IT("mouse buttons use the same source and frame edge contract", {
        InputTests::PollingAdapter input;
        input.Update();
        input.held[FW_KEY_RBUTTON] = true;
        input.Update();
        EQUALS(input.IsMouseButtonDown(1), true);
        EQUALS(input.IsMouseButtonPressed(1), true);
        EQUALS(input.IsKeyDown(FW_KEY_RBUTTON), true);
        input.held[FW_KEY_RBUTTON] = false;
        input.Update();
        EQUALS(input.IsMouseButtonReleased(1), true);
        EQUALS(input.IsMouseButtonDown(-1), false);
        EQUALS(input.IsMouseButtonUp(5), false);
    });
});
