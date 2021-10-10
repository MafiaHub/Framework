/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "api/api.h"
#include "backend/d3d9.h"
#include "backend/d3d11.h"
#include "errors.h"
#include "types.h"

namespace Framework::GUI {
    struct RendererConfiguration {
        RendererBackend backend; 
        bool cef = false;
        bool imgui = false;
        bool ultralight = false;
    };

    class Renderer {
      private:
        RendererConfiguration _config;
        RendererState _state;
        RendererBackend _backend;

        API _cefApi;
        API _imguiApi;
        API _ultralightApi;

        D3D9Backend *_d3d9Backend;
        D3D11Backend *_d3d11Backend;


        bool _initialized = false;

      public:
        RendererError Init(RendererConfiguration);
        RendererError Shutdown();

        void HandleDeviceLost();
        void HandleDeviceReset();

        RendererState GetCurrentState() const {
            return _state;
        }

        D3D9Backend* GetD3D9Backend() const {
            return _d3d9Backend;
        }

        D3D11Backend *GetD3D11Backend() const {
            return _d3d11Backend;
        }

        bool IsInitialized() const {
            return _initialized;
        }
    };
} // namespace Framework::GUI
