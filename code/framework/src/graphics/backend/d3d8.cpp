/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d8.h"

namespace Framework::Graphics {
    bool D3D8Backend::Init(const Framework::Graphics::RendererConfiguration &opts) {
        _device = opts.d3d8.device;
        return _device != nullptr;
    }

    void D3D8Backend::Shutdown() {
        _device = nullptr;
    }

    void D3D8Backend::Update() {}

    bool D3D8Backend::IsDeviceReady() const {
        return _device && SUCCEEDED(_device->TestCooperativeLevel());
    }

    bool D3D8Backend::GetBackBufferSize(int &width, int &height) const {
        if (!_device) {
            return false;
        }
        D3D8::Surface *surface = nullptr;
        if (FAILED(_device->GetBackBuffer(0, D3D8::kBackBufferTypeMono, &surface)) || !surface) {
            return false;
        }
        D3D8::SurfaceDesc desc {};
        const HRESULT result = surface->GetDesc(&desc);
        surface->Release();
        if (FAILED(result) || desc.width == 0 || desc.height == 0) {
            return false;
        }
        width  = static_cast<int>(desc.width);
        height = static_cast<int>(desc.height);
        return true;
    }
} // namespace Framework::Graphics
