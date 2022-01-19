/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "renderer.h"

#include <optick.h>

namespace Framework::Graphics {
    RendererError Renderer::Init(RendererConfiguration config) {
        if (_initialized) {
            return RendererError::RENDERER_ALREADY_INITIALIZED;
        }

        _config = config;

        if (config.backend == RendererBackend::BACKEND_D3D_11) {
            _d3d11Backend = new D3D11Backend;
        }
        else if (config.backend == RendererBackend::BACKEND_D3D_9) {
            _d3d9Backend = new D3D9Backend;
        }

        _initialized = true;
        return RendererError::RENDERER_NONE;
    }

    RendererError Renderer::Shutdown() {
        if (!_initialized) {
            return RendererError::RENDERER_NOT_INITIALIZED;
        }

        if (_d3d11Backend) {
            _d3d11Backend->Shutdown();
        }
        else if (_d3d9Backend) {
            _d3d9Backend->Shutdown();
        }

        _initialized = false;
        return RendererError::RENDERER_NONE;
    }

    void Renderer::Update() {
        OPTICK_EVENT();
        if (_d3d11Backend) {
            _d3d11Backend->Update();
        }
        else if (_d3d9Backend) {
            _d3d9Backend->Update();
        }
    }
} // namespace Framework::Graphics
