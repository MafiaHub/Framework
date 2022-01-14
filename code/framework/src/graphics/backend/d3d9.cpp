/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d9.h"

namespace Framework::Graphics {
    bool D3D9Backend::Init(IDirect3DDevice9 *device, void *_nothing) {
        _device = device;

        return true;
    }

    bool D3D9Backend::Shutdown() {
        return true;
    }

    void D3D9Backend::Update() {}
} // namespace Framework::Graphics
