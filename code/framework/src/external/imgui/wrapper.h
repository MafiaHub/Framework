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

#include "graphics/types.h"

#include <SDL2/SDL.h>

#include <atomic>
#include <function2.hpp>
#include <mutex>
#include <queue>

namespace Framework::Graphics {
    class Renderer;
} // namespace Framework::Graphics

namespace Framework::External::ImGUI {
    enum class InputState {
        BLOCK,
        PASS,
        ERROR_MISMATCH
    };

    struct Config {
        Graphics::PlatformBackend windowBackend = Graphics::PlatformBackend::PLATFORM_WIN32;
        Graphics::RendererBackend renderBackend = Graphics::RendererBackend::BACKEND_D3D_11;

        // NOTE: Set up during init
        Graphics::Renderer *renderer = nullptr;
        SDL_Window *sdlWindow        = nullptr;
        HWND windowHandle            = nullptr;
    };

    class Wrapper final {
      public:
        using RenderProc = fu2::function<void() const>;

      private:
        Config _config;
        bool _initialized = false;
        std::queue<RenderProc> _renderQueue;
        std::recursive_mutex _renderMtx;

        static inline std::atomic_bool isContextInitialized = false;

        bool _processEventEnabled = true;

      public:
        Error Init(Config &config);
        Error Shutdown();

        InputState ProcessEvent(const SDL_Event *event) const;
        InputState ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) const;
        static void ShowCursor(bool show);

        Error Update();
        Error Render();

        void PushWidget(const RenderProc &proc) {
            _renderQueue.push(proc);
        }

        bool IsInitialized() const {
            return _initialized;
        }

        /**
         * This function allows you to enable/disable mouse and keyboard inputs.
         * By default, process event is enable.
         */
        void SetProcessEventEnabled(bool enable) {
            _processEventEnabled = enable;
        }

        bool IsProcessEventEnabled() const {
            return _processEventEnabled;
        }
    };
} // namespace Framework::External::ImGUI
