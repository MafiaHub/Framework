/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
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
        virtual ~IInput() {}
        virtual void Update() = 0;

        virtual void SetMousePosition(int x, int y)   = 0;
        virtual void GetMousePosition(int &x, int &y) = 0;
        virtual void SetMouseVisible(bool visible)    = 0;
        virtual bool IsMouseVisible()                 = 0;
        virtual void SetMouseLocked(bool locked)      = 0;
        virtual bool IsMouseLocked()                  = 0;

        virtual bool IsKeyDown(int key)     = 0;
        virtual bool IsKeyUp(int key)       = 0;
        virtual bool IsKeyPressed(int key)  = 0;
        virtual bool IsKeyReleased(int key) = 0;

        virtual bool IsMouseButtonDown(int button)     = 0;
        virtual bool IsMouseButtonUp(int button)       = 0;
        virtual bool IsMouseButtonPressed(int button)  = 0;
        virtual bool IsMouseButtonReleased(int button) = 0;

        virtual uint32_t MapKey(uint32_t key) = 0;
    };
} // namespace Framework::Input
