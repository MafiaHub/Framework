/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "renderer.h"

#include "backend/d3d11.h"
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

    void Renderer::Paint() {
        ForActiveBackend([](auto &backend) {
            backend.Paint();
        });
    }
} // namespace Framework::Graphics
