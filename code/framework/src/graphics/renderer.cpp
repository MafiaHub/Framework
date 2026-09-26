/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "renderer.h"

#include "backend/d3d11.h"
#include "backend/d3d8.h"
#include "backend/d3d12.h"
#include "backend/d3d9.h"

namespace Framework::Graphics {
    Renderer::Renderer()  = default;
    Renderer::~Renderer() = default;

    Utils::Result<void, Error> Renderer::Init(RendererConfiguration config) {
        if (_initialized) {
            return Error("Renderer is already initialized");
        }

        _config  = config;
        _backend = config.backend;

        if (_config.backend == RendererBackend::BACKEND_D3D_11) {
            _d3d11Backend = std::make_unique<D3D11Backend>();
            if (!_d3d11Backend->Init(_config)) {
                _d3d11Backend.reset();
                return Error("Failed to initialize the D3D11 graphics backend");
            }
        }
        else if (_config.backend == RendererBackend::BACKEND_D3D_9) {
            _d3d9Backend = std::make_unique<D3D9Backend>();
            if (!_d3d9Backend->Init(_config)) {
                _d3d9Backend.reset();
                return Error("Failed to initialize the D3D9 graphics backend");
            }
        }
        else if (_config.backend == RendererBackend::BACKEND_D3D_12) {
            _d3d12Backend = std::make_unique<D3D12Backend>();
            if (!_d3d12Backend->Init(_config)) {
                _d3d12Backend.reset();
                return Error("Failed to initialize the D3D12 graphics backend");
            }
        }
        else if (_config.backend == RendererBackend::BACKEND_D3D_8) {
            _d3d8Backend = std::make_unique<D3D8Backend>();
            if (!_d3d8Backend->Init(_config)) {
                _d3d8Backend.reset();
                return Error("Failed to initialize the D3D8 graphics backend");
            }
        }
        else {
            return Error("Renderer backend is not implemented");
        }

        _initialized = true;
        return {};
    }

    template <typename Fn>
    void Renderer::ForActiveBackend(Fn &&fn) {
        if (_d3d11Backend) {
            fn(*_d3d11Backend);
        }
        else if (_d3d9Backend) {
            fn(*_d3d9Backend);
        }
        else if (_d3d12Backend) {
            fn(*_d3d12Backend);
        }
        else if (_d3d8Backend) {
            fn(*_d3d8Backend);
        }
    }

    void Renderer::Shutdown() {
        if (!_initialized) {
            return;
        }

        ForActiveBackend([](auto &backend) {
            backend.Shutdown();
        });

        Lifecycle::Shutdown();
    }

    void Renderer::Update() {
        ForActiveBackend([](auto &backend) {
            backend.Update();
        });
    }

    void Renderer::Render() {
        ForActiveBackend([](auto &backend) {
            backend.Render();
        });
    }

    bool Renderer::GetBackBufferSize(int &width, int &height) const {
        if (_d3d8Backend) {
            return _d3d8Backend->GetBackBufferSize(width, height);
        }

        IDXGISwapChain *swapChain = nullptr;
        if (_d3d12Backend) {
            swapChain = _d3d12Backend->GetSwapChain();
        }
        else if (_d3d11Backend) {
            swapChain = _d3d11Backend->GetSwapChain();
        }

        DXGI_SWAP_CHAIN_DESC desc {};
        if (!swapChain || FAILED(swapChain->GetDesc(&desc)) || desc.BufferDesc.Width == 0 || desc.BufferDesc.Height == 0) {
            return false;
        }

        width  = static_cast<int>(desc.BufferDesc.Width);
        height = static_cast<int>(desc.BufferDesc.Height);
        return true;
    }

    void Renderer::Paint() {
        ForActiveBackend([](auto &backend) {
            backend.Paint();
        });
    }
} // namespace Framework::Graphics
