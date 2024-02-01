/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "utils/safe_win32.h"

#include <SDL2/SDL.h>

namespace Framework::External::SDL2 {
    class Wrapper final {
      private:
        HWND _windowHandle  = nullptr;
        SDL_Window *_window = nullptr;

      public:
        Error Init(HWND windowHandle);
        Error Shutdown() const;

        static SDL_Event PollEvent();

        SDL_Window *GetWindow() const {
            return _window;
        }
    };
} // namespace Framework::External::SDL2
