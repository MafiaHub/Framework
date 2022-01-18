/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "backend/d3d11.h"
#include "backend/d3d9.h"
#include "errors.h"
#include "types.h"

#include <Windows.h>

namespace Framework::Graphics {
    struct RendererConfiguration {
        RendererBackend backend;
        PlatformBackend platform;
        HWND windowHandle;

        union {
            struct sd3d9 {
                IDirect3DDevice9 *device;
            } d3d9;
            struct sd3d11 {
                ID3D11Device *device;
                ID3D11DeviceContext *deviceContext;
            } d3d11;
        };
    };

    class Renderer {
      private:
        RendererConfiguration _config;
        RendererState _state;
        RendererBackend _backend;

        HWND _window;

        D3D9Backend *_d3d9Backend;
        D3D11Backend *_d3d11Backend;

        bool _initialized = false;

      public:
        RendererError Init(RendererConfiguration);
        RendererError Shutdown();

        void Update();

        void HandleDeviceLost();
        void HandleDeviceReset();

        RendererState GetCurrentState() const {
            return _state;
        }

        D3D9Backend *GetD3D9Backend() const {
            return _d3d9Backend;
        }

        D3D11Backend *GetD3D11Backend() const {
            return _d3d11Backend;
        }

        HWND GetWindow() const {
            return _window;
        }

        void SetWindow(HWND ptr) {
            _window = ptr;
        }

        bool IsInitialized() const {
            return _initialized;
        }
    };
} // namespace Framework::Graphics
