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

#include "graphics/types.h"

#include <SDL.h>

#include <functional>
#include <queue>
#include <mutex>

namespace Framework::Graphics {
    class Renderer;
} // namespace Framework::Graphics

namespace Framework::External::ImGUI {
    enum class InputState { BLOCK, PASS, ERROR_MISMATCH };

    struct Config {
        Framework::Graphics::PlatformBackend windowBackend = Framework::Graphics::PlatformBackend::PLATFORM_WIN32;
        Framework::Graphics::RendererBackend renderBackend = Framework::Graphics::RendererBackend::BACKEND_D3D_11;

        // NOTE: Set up during init
        Graphics::Renderer *renderer = nullptr;
        SDL_Window *sdlWindow        = nullptr;
        HWND windowHandle            = 0;
    };

    class Wrapper final {
      public:
        using RenderProc = std::function<void()>;
      private:
        Config _config;
        bool _initialized = false;
        std::queue<RenderProc> _renderQueue;
        std::recursive_mutex _renderMtx;

      public:
        Error Init(Config &config);
        Error Shutdown();

        InputState ProcessEvent(const SDL_Event *event);
        InputState ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
        void ShowCursor(bool show);

        Error Update();
        Error Render();

        inline void PushWidget(RenderProc proc) {
            _renderQueue.push(proc);
        }

        inline bool IsInitialized() const {
            return _initialized;
        };
    };
} // namespace Framework::External::ImGUI
