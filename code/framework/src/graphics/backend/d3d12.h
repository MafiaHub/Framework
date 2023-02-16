/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "backend.h"

#ifdef WIN32
#include <d3d12.h>
#else
#define ID3D12Device        void
#endif
#define ID3D12DeviceContext void

namespace Framework::Graphics {
    class D3D12Backend: public Backend<ID3D12Device *, ID3D12DeviceContext *> {
      private:
        ID3D12DescriptorHeap *_srvHeap = nullptr;
      public:
        bool Init(ID3D12Device *, ID3D12DeviceContext *) override;
        bool Shutdown() override;
        void Update() override;

        int NumFramesInFlight() const;

        ID3D12DescriptorHeap *GetSRVHeap() const {
            return _srvHeap;
        }
    };
} // namespace Framework::Graphics
