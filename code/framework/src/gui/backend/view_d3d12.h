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
    // CPU-path D3D12 view: uploads CEF's OnPaint pixel buffer into a texture on
    // the backend command list (render thread, between D3D12Backend::Begin and
    // the ImGui draw) and blits it via the ImGui background draw list (game
    // thread, inside the ImGui frame). The GPU-accelerated CEF path is not
    // supported here — CEF shares D3D11 textures, which would require interop.
    class ViewD3D12 final: public View {
      private:
        Microsoft::WRL::ComPtr<ID3D12Resource> _texture;

        // One upload buffer per frame in flight so we never write a buffer the
        // GPU may still be copying from
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

        [[nodiscard]] GUIError Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated = false) override;

        void Update() override;
        void Render() override;
        void SubmitImGuiDraw() override;
        std::string GetDebugString() const override;

      private:
        bool CreateResources();
        void ReleaseResources();
        void UploadPixels(const std::vector<uint8_t> &pixels);
    };
} // namespace Framework::GUI
