/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "backend.h"

#ifdef WIN32
#define _WINSOCKAPI_
#include <d3d9.h>
#else
#define IDirect3D9 void
#endif

namespace Framework::Graphics {
    class D3D9Backend: public Backend<IDirect3D9 *, IDirect3D9 *> {
      public:
        virtual bool Init(IDirect3D9 *, IDirect3D9*) override;
        virtual bool Shutdown() override;

        virtual void Update() override;
    };
} // namespace Framework::GUI
