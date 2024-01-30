/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/safe_win32.h"

#include "backend/d3d11.h"
#include "backend/d3d12.h"
#include "backend/d3d9.h"
#include "errors.h"
#include "types.h"

namespace Framework::Graphics {
    struct RendererConfiguration {
        RendererBackend backend;
        PlatformBackend platform;
        HWND windowHandle;

        struct {
            IDirect3DDevice9 *device;
        } d3d9;
        struct {
            ID3D11Device *device;
            ID3D11DeviceContext *deviceContext;
        } d3d11;

        struct {
            ID3D12Device *device             = nullptr;
            IDXGISwapChain3 *swapchain       = nullptr;
            ID3D12CommandQueue *commandQueue = nullptr;
            // todo
        } d3d12;
    };

    class Renderer {
      private:
        RendererConfiguration _config {};
        RendererState _state     = RendererState::STATE_NOT_INITIALIZED;
        RendererBackend _backend = RendererBackend::BACKEND_D3D_11;

        HWND _window {};

        D3D9Backend *_d3d9Backend {};
        D3D11Backend *_d3d11Backend {};
        D3D12Backend *_d3d12Backend {};

        bool _initialized = false;

      public:
        RendererError Init(RendererConfiguration);
        RendererError Shutdown();

        void Update();

        RendererState GetCurrentState() const {
            return _state;
        }

        D3D9Backend *GetD3D9Backend() const {
            return _d3d9Backend;
        }

        D3D11Backend *GetD3D11Backend() const {
            return _d3d11Backend;
        }

        D3D12Backend *GetD3D12Backend() const {
            return _d3d12Backend;
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
