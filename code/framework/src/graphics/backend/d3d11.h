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
#include <d3d11.h>
#else
#define ID3D11Device        void
#define ID3D11DeviceContext void
#endif

namespace Framework::Graphics {
    class D3D11Backend: public Backend<ID3D11Device *, ID3D11DeviceContext *, IDXGISwapChain *, void *> {
      private:
        IDXGISwapChain *_swapChain {};
      public:
        bool Init(ID3D11Device *, ID3D11DeviceContext *, IDXGISwapChain *, void *) override;
        bool Shutdown() override;
        void Update() override;

        IDXGISwapChain* GetSwapChain() const {
            return _swapChain;
        }
    };
} // namespace Framework::Graphics
