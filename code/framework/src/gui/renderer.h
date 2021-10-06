/*
 * MafiaHub OSS license
 * Copyright (c) 2021 MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

namespace Framework::GUI {
    enum class RendererBackend { BACKEND_D3D_9, BACKEND_D3D_11, BACKEND_D3D_12 };

    enum class RendererAPI { CEF, ULTRALIGHT };

    enum class RendererState { STATE_NOT_INITIALIZED, STATE_READY, STATE_DEVICE_LOST, STATE_DEVICE_NOT_RESET, STATE_DRIVER_ERROR };

    class Renderer {
      private:
        RendererBackend _backend;
        RendererAPI _api;
        RendererState _state;

      public:
        RendererError Init(RendererAPI, RendererBackend);
        RendererError Shutdown();

        void HandleDeviceLost();
        void HandleDeviceReset();

        RendererAPI GetRendererAPI() const {
          return _api;
        }

        RendererBackend GetBackend() const {
            return _backend;
        }

        RendererState GetState() const {
            return _state;
        }

      private:
        bool InitUltraLight();
        bool InitCEF();
    }
} // namespace Framework::GUI