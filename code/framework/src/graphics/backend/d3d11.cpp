/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d11.h"

namespace Framework::Graphics {
    bool D3D11Backend::Init(ID3D11Device *device, ID3D11DeviceContext *context) {
        _device  = device;
        _context = context;

        return true;
    }

    bool D3D11Backend::Shutdown() {
        return true;
    }

    void D3D11Backend::Update() {}
} // namespace Framework::Graphics
