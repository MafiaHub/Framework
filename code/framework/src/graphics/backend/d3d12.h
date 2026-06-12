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
#include <vector>

namespace Framework::Graphics {
    class D3D12Backend: public Backend<ID3D12Device *, ID3D12DeviceContext *, IDXGISwapChain3 *, ID3D12CommandQueue *> {
      public:
        // Extra shader-visible SRV slots reserved after the ImGui font slot(s),
        // handed out to web views via AllocateSRVSlot
        static constexpr UINT kExtraSrvSlots = 64;

      private:
        IDXGISwapChain3 *_swapChain             = nullptr;
        UINT _frameBufferCount                  = 0;
        ID3D12DescriptorHeap *_rtvHeap          = nullptr;
        ID3D12DescriptorHeap *_srvHeap          = nullptr;
        ID3D12GraphicsCommandList *_commandList = nullptr;
        ID3D12CommandQueue *_commandQueue       = nullptr;

        UINT _srvDescriptorSize = 0;
        std::vector<UINT> _freeSrvSlots;

        struct FrameContext {
            ID3D12CommandAllocator *_commandAllocator = nullptr;
            ID3D12Resource *_mainRenderTargetResource = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE _mainRenderTargetDescriptor;
        };

        std::vector<FrameContext> _frameContext;
        D3D12_RESOURCE_BARRIER _barrier {};

      public:
        bool Init(const Framework::Graphics::RendererConfiguration &opts) override;
        void Shutdown() override;
        void Update() override;
        void Begin();
        void End();
        int NumFramesInFlight() const;

        // TODO: Backend not implemented yet
        void BeginDrawing() {}
        void EndDrawing() {}
        void BindTexture(uint8_t texture_unit, uint32_t texture_id) {}
        void BindRenderBuffer(uint32_t render_buffer_id) {}
        void ClearRenderBuffer(uint32_t render_buffer_id) {}
        void DrawGeometry(uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const GPUState &state) {}
        void CreateTexture(uint32_t texture_id, Bitmap bitmap) {};
        void UpdateTexture(uint32_t texture_id, Bitmap bitmap) {};
        void DestroyTexture(uint32_t texture_id) {};
        void CreateRenderBuffer(uint32_t render_buffer_id, const RenderBuffer &buffer) {};
        void DestroyRenderBuffer(uint32_t render_buffer_id) {};
        void CreateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices) {};
        void UpdateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices) {};
        void DestroyGeometry(uint32_t geometry_id) {};
        void SetViewport(uint32_t width, uint32_t height) {};
        glm::mat4 ApplyProjection(const glm::mat4 &transform, float screen_width, float screen_height) {
            return {};
        };
        ID3D12DescriptorHeap *GetSRVHeap() const {
            return _srvHeap;
        }

        ID3D12GraphicsCommandList *GetGraphicsCommandList() const {
            return _commandList;
        }

        UINT GetCurrentFrameIndex() const {
            return _swapChain ? _swapChain->GetCurrentBackBufferIndex() : 0;
        }

        // Allocates a shader-visible SRV slot from the backend heap (the one ImGui
        // is initialized against, so handles are usable as ImTextureID). Returns -1
        // when exhausted.
        int AllocateSRVSlot();
        void FreeSRVSlot(int slot);
        D3D12_CPU_DESCRIPTOR_HANDLE GetSRVSlotCPUHandle(int slot) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVSlotGPUHandle(int slot) const;

        // Blocks until the GPU has drained the queue. Used before destroying
        // resources that in-flight command lists may still reference. Returns
        // false when the drain could not be confirmed (fence timeout/failure)
        // so callers can refuse to free still-referenced resources; returns
        // true during process teardown, where waiting is impossible and
        // freeing is safe (the queue's threads are already gone).
        [[nodiscard]] bool WaitForGpu();

        size_t GetFreeSRVSlotCount() const {
            return _freeSrvSlots.size();
        }
    };
} // namespace Framework::Graphics
