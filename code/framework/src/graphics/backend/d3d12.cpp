/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d12.h"

namespace Framework::Graphics {
    bool D3D12Backend::Init(ID3D12Device *device, ID3D12DeviceContext *context) {
        _device  = device;
        _context = context;

        return true;
    }

    bool D3D12Backend::Shutdown() {
        return true;
    }

    void D3D12Backend::Update() {}
} // namespace Framework::Graphics
