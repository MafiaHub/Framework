/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <d3d12.h>
#include <mutex>
#include <vector>
#include <wrl/client.h>

#include "graphics/renderer.h"
#include "gui/view.h"

#include "include/cef_render_handler.h"

namespace Framework::GUI {
    // D3D12 view. CPU path: uploads CEF's OnPaint buffer to a texture on the render
    // thread. GPU path: copies CEF's shared texture out on the pump thread, then
    // into the same texture on the render thread. Either way it is blitted via the
    // ImGui background draw list.
    class ViewD3D12 final: public View {
      private:
        Microsoft::WRL::ComPtr<ID3D12Resource> _texture;

        // one per frame in flight, so we never write one the GPU is still copying
        struct UploadBuffer {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            uint8_t *mapped = nullptr;
        };
        std::vector<UploadBuffer> _uploadBuffers;

        int _texWidth  = 0;
        int _texHeight = 0;
        uint32_t _uploadPitch = 0;
        D3D12_RESOURCE_STATES _textureState = D3D12_RESOURCE_STATE_COPY_DEST;

        int _srvSlot = -1;
        D3D12_GPU_DESCRIPTOR_HANDLE _srvGpuHandle {};

        // Set once the texture holds at least one full frame of pixels
        bool _textureReady = false;

        // GPU path. CEF's texture goes back to its pool when OnAcceleratedPaint
        // returns, so the pump thread copies it on its own queue and waits for that
        // copy. The page and popup are kept apart and composed into a staging ring;
        // the render thread copies the newest staging texture on the game's list.
        struct SharedCopy {
            Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
            Microsoft::WRL::ComPtr<ID3D12Fence> fence;
            UINT64 fenceValue = 0;
            HANDLE event      = nullptr;
        };
        SharedCopy _sharedCopy;

        struct SharedTexture {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            UINT width  = 0;
            UINT height = 0;
        };
        SharedTexture _sharedPage;
        SharedTexture _sharedPopup;

        // A staging texture is free again once the game frame that copied from
        // it has run, i.e. the backend's frame fence reached readFence.
        struct StagingTexture {
            SharedTexture texture;
            UINT64 readFence = 0;
        };
        std::vector<StagingTexture> _staging;

        // Guards _sharedLatest, _sharedDirty and every readFence
        std::mutex _sharedMutex;
        int _sharedLatest = -1;
        bool _sharedDirty = false;

      public:
        ViewD3D12(int id, Graphics::Renderer *graphicsRenderer, Manager *manager);
        ~ViewD3D12() override;

        [[nodiscard]] Utils::Result<void, Framework::Error> Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated = false) override;

        void Render() override;
        void SubmitImGuiDraw() override;

      private:
        bool CreateResources(bool uploadBuffers);
        void ReleaseResources();
        // Copies pixels into the texture. Returns false (a no-op) when the command list or resources
        // aren't ready, so the caller keeps the dirty flag set and retries on a later frame.
        bool UploadPixels(const std::vector<uint8_t> &pixels);

        bool CreateSharedCopy(ID3D12Device *device);
        void ReleaseSharedCopy();
        void CopySharedFrame(CefRenderHandler::PaintElementType type, HANDLE handle);
        void RenderShared(Graphics::D3D12Backend *backend);
    };
} // namespace Framework::GUI
