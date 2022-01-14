/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "utils/safe_win32.h"

#include <SDL.h>

namespace Framework::External::SDL2 {
    class Wrapper final {
      private:
        HWND _windowHandle  = 0;
        SDL_Window *_window = nullptr;

      public:
        Error Init(HWND windowHandle);
        Error Shutdown();

        const SDL_Event PollEvent();

        inline SDL_Window *GetWindow() {
            return _window;
        }
    };
} // namespace Framework::External::SDL2
