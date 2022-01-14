/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "graphics/renderer.h"

#include <SDL.h>

namespace Framework::External::ImGUI {
    enum class RenderBackend {
        D3D9,
        D3D11,
        // D3D12,
    };

    enum class WindowBackend { WIN_32, SDL2 };

    struct Config {
        WindowBackend windowBackend = WindowBackend::WIN_32;
        RenderBackend renderBackend = RenderBackend::D3D11;

        // NOTE: Set up during init
        Graphics::Renderer *renderer = nullptr;
        SDL_Window *sdlWindow        = nullptr;
        HWND windowHandle            = 0;
    };

    class Wrapper final {
      private:
        Config _config;

      public:
        Error Init(Config &config);
        Error Shutdown();

        Error ProcessEvent(const SDL_Event *event);
        Error ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

        Error NewFrame();
        Error EndFrame();
        Error Render();
    };
} // namespace Framework::External::ImGUI
