/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "wrapper.h"

#include <optick.h>

namespace Framework::External::SDL2 {
    Error Wrapper::Init(HWND windowHandle) {
        if (!windowHandle) {
            return Error::ERROR_WINDOW_INVALID_HANDLE;
        }
        _windowHandle = windowHandle;

        if (!SDL_Init(SDL_INIT_EVERYTHING)) {
            return Error::ERROR_INIT_FAILED;
        }

        _window = SDL_CreateWindowFrom(windowHandle);

        if (!_window) {
            return Error::ERROR_WINDOW_CREATION_FAILED;
        }

        return Error::ERROR_NONE;
    }

    Error Wrapper::Shutdown() {
        SDL_DestroyWindow(_window);
        SDL_Quit();

        return Error::ERROR_NONE;
    }

    const SDL_Event Wrapper::PollEvent() {
        OPTICK_EVENT();
        SDL_Event event;
        SDL_PollEvent(&event);
        return event;
    }

} // namespace Framework::External::SDL2
