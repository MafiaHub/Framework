/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "api/api.h"
#include "errors.h"
#include "types.h"

namespace Framework::GUI {
    struct RendererConfiguration {
        RendererAPI api;
        RendererBackend backend;
    };

    class Renderer {
      private:
        RendererConfiguration _config;
        RendererState _state;

        API _api;

        bool _initialized = false;

      public:
        RendererError Init(RendererConfiguration);
        RendererError Shutdown();

        void HandleDeviceLost();
        void HandleDeviceReset();

        RendererState GetCurrentState() const {
            return _state;
        }

        API GetAPI() const {
            return _api;
        }

        bool IsInitialized() const {
            return _initialized;
        }
    };
} // namespace Framework::GUI
