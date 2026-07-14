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
#include <mutex>
#include <vector>

namespace Framework::Graphics {
    class D3D12Backend final: public Backend<ID3D12Device *, ID3D12DeviceContext *, IDXGISwapChain3 *, ID3D12CommandQueue *> {
      public:
        // Shader-visible SRV slots reserved past the ImGui font descriptors,
        // handed to web views via AllocateSRVSlot.
        static constexpr UINT kExtraSrvSlots = 64;

      private:
        IDXGISwapChain3 *_swapChain             = nullptr;
        UINT _frameBufferCount                  = 0;
        ID3D12DescriptorHeap *_rtvHeap          = nullptr;
        ID3D12DescriptorHeap *_srvHeap          = nullptr;
        ID3D12GraphicsCommandList *_commandList = nullptr;
        ID3D12CommandQueue *_commandQueue       = nullptr;

        UINT _srvDescriptorSize = 0;
        UINT _srvHeapSize       = 0;
        std::vector<UINT> _freeSrvSlots;
        std::vector<bool> _srvSlotInUse;
        mutable std::mutex _srvMutex;

        struct FrameContext {
            ID3D12CommandAllocator *_commandAllocator = nullptr;
            D3D12_CPU_DESCRIPTOR_HANDLE _mainRenderTargetDescriptor;
        };

        std::vector<FrameContext> _frameContext;
        D3D12_RESOURCE_BARRIER _barrier {};

        // acquired fresh in Begin(), released in End(); never held across frames
        // so the game can resize/recreate the swapchain freely
        ID3D12Resource *_currentBackBuffer = nullptr;

      public:
        bool Init(const Framework::Graphics::RendererConfiguration &opts) override;
        void Shutdown() override;
        void Update() override;
        void Begin();
        void End();
        int NumFramesInFlight() const;

        // Repoint after the game recreates its swapchain (fullscreen / mode
        // toggles). Assumes the same buffer count as Init — Begin() guards against
        // a larger one rather than rebuilding the frame state.
        IDXGISwapChain3 *GetSwapChain() const {
            return _swapChain;
        }
        void SetSwapChain(IDXGISwapChain3 *swapChain) {
            _swapChain = swapChain;
        }

        // Bounded, shader-visible SRV slot pool shared with ImGui's heap (so handles
        // double as ImTextureID). AllocateSRVSlot returns -1 when exhausted; the
        // getters return a null handle for any out-of-range slot.
        int AllocateSRVSlot();

        // Frees an SRV slot from a given slot handle
        void FreeSRVSlot(int slot);

        // Retrieves a CPU Descriptor handle from a given @p slot handle
        D3D12_CPU_DESCRIPTOR_HANDLE GetSRVSlotCPUHandle(int slot) const;

        // Retrieves a GPU Descriptor handle from a given @p slot handle
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVSlotGPUHandle(int slot) const;

        // Returns number of free SRV slots
        size_t GetFreeSRVSlotCount() const;

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

        ID3D12CommandQueue *GetCommandQueue() const {
            return _commandQueue;
        }

        ID3D12GraphicsCommandList *GetGraphicsCommandList() const {
            return _commandList;
        }

        UINT GetCurrentFrameIndex() const {
            return _swapChain ? _swapChain->GetCurrentBackBufferIndex() : 0;
        }

        // Drain the GPU queue before freeing resources in-flight lists may use.
        // Returns false if the drain can't be confirmed (caller should keep the
        // resources); returns true during teardown, where freeing is always safe.
        [[nodiscard]] bool WaitForGpu();
    };
} // namespace Framework::Graphics
