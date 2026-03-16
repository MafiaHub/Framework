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

    RendererError Renderer::Init(RendererConfiguration config) {
        if (_initialized) {
            return RendererError::RENDERER_ALREADY_INITIALIZED;
        }

        _config = config;

        if (_config.backend == RendererBackend::BACKEND_D3D_11) {
            _d3d11Backend = std::make_unique<D3D11Backend>();
            if (!_d3d11Backend->Init(_config)) {
                _d3d11Backend.reset();
                return RendererError::RENDERER_BACKEND_INIT_FAILED;
            }
        }
        else if (_config.backend == RendererBackend::BACKEND_D3D_9) {
            _d3d9Backend = std::make_unique<D3D9Backend>();
            if (!_d3d9Backend->Init(_config)) {
                _d3d9Backend.reset();
                return RendererError::RENDERER_BACKEND_INIT_FAILED;
            }
        }
        else if (_config.backend == RendererBackend::BACKEND_D3D_12) {
            _d3d12Backend = std::make_unique<D3D12Backend>();
            if (!_d3d12Backend->Init(_config)) {
                _d3d12Backend.reset();
                return RendererError::RENDERER_BACKEND_INIT_FAILED;
            }
        }

        _initialized = true;
        return RendererError::RENDERER_NONE;
    }

    void Renderer::Shutdown() {
        if (!_initialized) {
            return;
        }

        if (_d3d11Backend) {
            _d3d11Backend->Shutdown();
        }
        else if (_d3d9Backend) {
            _d3d9Backend->Shutdown();
        }
        else if (_d3d12Backend) {
            _d3d12Backend->Shutdown();
        }

        Lifecycle::Shutdown();
    }

    void Renderer::Update() {
        if (_d3d11Backend) {
            _d3d11Backend->Update();
        }
        else if (_d3d9Backend) {
            _d3d9Backend->Update();
        }
        else if (_d3d12Backend) {
            _d3d12Backend->Update();
        }
    }

    void Renderer::Render() {
        if (_d3d11Backend) {
            _d3d11Backend->Render();
        }
        else if (_d3d9Backend) {
            _d3d9Backend->Render();
        }
        else if (_d3d12Backend) {
            _d3d12Backend->Render();
        }
    }
    
    void Renderer::Paint() {
        if (_d3d11Backend) {
            _d3d11Backend->Paint();
        }
        else if (_d3d9Backend) {
            _d3d9Backend->Paint();
        }
        else if (_d3d12Backend) {
            _d3d12Backend->Paint();
        }
    }
} // namespace Framework::Graphics
