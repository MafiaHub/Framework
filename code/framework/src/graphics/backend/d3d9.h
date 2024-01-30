/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "backend.h"

#ifdef WIN32
#include <d3d9.h>
#else
#define IDirect3D9 void
#endif

namespace Framework::Graphics {
    class D3D9Backend : public Backend<IDirect3DDevice9 *, void *, void *, void *> {
      public:
        bool Init(IDirect3DDevice9 *, void *, void *, void *) override;
        bool Shutdown() override;
        void Update() override;
    };
} // namespace Framework::Graphics
