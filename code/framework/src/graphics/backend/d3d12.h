/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "backend.h"

#include <memory>

#ifdef WIN32
#include <d3d12.h>
#include <dxgi1_4.h>
#else
#define ID3D12Device        void
#endif
#define ID3D12DeviceContext void

#include <vector>

namespace Framework::Graphics {
    class D3D12Backend: public Backend<ID3D12Device *, ID3D12DeviceContext *, IDXGISwapChain3 *, ID3D12CommandQueue *> {
      private:
        IDXGISwapChain3* _swapChain = nullptr;
        UINT _frameBufferCount = 0;
        ID3D12DescriptorHeap* _rtvHeap = nullptr;
	    ID3D12DescriptorHeap* _srvHeap = nullptr;
        ID3D12GraphicsCommandList* _commandList = nullptr;
        ID3D12CommandQueue* _commandQueue = nullptr;

        struct FrameContext {
          ID3D12CommandAllocator* _commandAllocator = nullptr;
          ID3D12Resource* _mainRenderTargetResource = nullptr;
          D3D12_CPU_DESCRIPTOR_HANDLE _mainRenderTargetDescriptor;
        };

        std::vector<FrameContext> _frameContext;
        D3D12_RESOURCE_BARRIER _barrier{};
      public:
        bool Init(ID3D12Device *, ID3D12DeviceContext *, IDXGISwapChain3 *, ID3D12CommandQueue *) override;
        bool Shutdown() override;
        void Update() override;
        void Begin();
        void End();
        int NumFramesInFlight() const;

        ID3D12DescriptorHeap *GetSRVHeap() const {
            return _srvHeap;
        }

        ID3D12GraphicsCommandList* GetGraphicsCommandList() const {
          return _commandList;
        }
    };
} // namespace Framework::Graphics
