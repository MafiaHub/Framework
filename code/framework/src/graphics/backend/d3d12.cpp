/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d12.h"

static int const NUM_FRAMES_IN_FLIGHT = 1;

namespace Framework::Graphics {
    bool D3D12Backend::Init(ID3D12Device *device, ID3D12DeviceContext *context) {
        _device  = device;
        _context = context;

        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_srvHeap)) != S_OK)
            return false;

        return true;
    }

    bool D3D12Backend::Shutdown() {
        return true;
    }

    void D3D12Backend::Update() {}

    int D3D12Backend::NumFramesInFlight() const {
        return NUM_FRAMES_IN_FLIGHT;
    }
} // namespace Framework::Graphics
