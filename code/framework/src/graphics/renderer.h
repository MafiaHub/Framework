/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/lifecycle.h>

#include "utils/safe_win32.h"

#include <utils/error.h>
#include <utils/result.h>

#include "types.h"

#include <memory>

#include <dxgi.h>

#ifdef WIN32
#include <d3d9.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#else
#define ID3D12Device void
#define IDirect3D9   void
#define ID3D11Device        void
#define ID3D11DeviceContext void
#endif
#define ID3D12DeviceContext void

namespace Framework::Graphics {
    class D3D9Backend;
    class D3D11Backend;
    class D3D12Backend;

    struct RendererConfiguration {
        RendererBackend backend {};
        PlatformBackend platform {};
        HWND windowHandle {};

        struct {
            IDirect3DDevice9 *device {};
        } d3d9;
        struct {
            ID3D11Device *device {};
            ID3D11DeviceContext *deviceContext {};
            IDXGISwapChain *swapChain {};
            bool useDeferredContext {};
        } d3d11;

        struct {
            ID3D12Device *device {};
            ID3D12DeviceContext *deviceContext {};
            IDXGISwapChain3 *swapchain {};
            ID3D12CommandQueue *commandQueue {};
        } d3d12;
    };

    class Renderer : public Framework::Lifecycle {
      private:
        RendererConfiguration _config {};
        RendererState _state     = RendererState::STATE_NOT_INITIALIZED;
        RendererBackend _backend = RendererBackend::BACKEND_D3D_11;

        HWND _window {};

        std::unique_ptr<D3D9Backend> _d3d9Backend;
        std::unique_ptr<D3D11Backend> _d3d11Backend;
        std::unique_ptr<D3D12Backend> _d3d12Backend;

      public:
        Renderer();
        ~Renderer();

        [[nodiscard]] Utils::Result<void, Error> Init(RendererConfiguration);
        void Shutdown() override;

        void Update() override;
        void Render();
        void Paint();

        RendererState GetCurrentState() const {
            return _state;
        }

        D3D9Backend *GetD3D9Backend() const {
            return _d3d9Backend.get();
        }

        D3D11Backend *GetD3D11Backend() const {
            return _d3d11Backend.get();
        }

        D3D12Backend *GetD3D12Backend() const {
            return _d3d12Backend.get();
        }

        HWND GetWindow() const {
            return _window;
        }

        void SetWindow(HWND ptr) {
            _window = ptr;
        }

        RendererBackend GetBackendType() const {
            return _backend;
        }
    };
} // namespace Framework::Graphics
