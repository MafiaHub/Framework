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

namespace Framework::GUI {
    // CPU-path D3D12 view: uploads CEF's OnPaint buffer to a texture on the render
    // thread and blits it via the ImGui background draw list on the game thread.
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

      public:
        ViewD3D12(int id, Graphics::Renderer *graphicsRenderer, Manager *manager);
        ~ViewD3D12() override;

        [[nodiscard]] Utils::Result<void, Framework::Error> Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated = false) override;

        void Update() override;
        void Render() override;
        void SubmitImGuiDraw() override;

      private:
        bool CreateResources();
        void ReleaseResources();
        // Copies pixels into the texture. Returns false (a no-op) when the command list or resources
        // aren't ready, so the caller keeps the dirty flag set and retries on a later frame.
        bool UploadPixels(const std::vector<uint8_t> &pixels);
    };
} // namespace Framework::GUI
