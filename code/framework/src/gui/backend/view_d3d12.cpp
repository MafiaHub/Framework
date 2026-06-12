/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "view_d3d12.h"
#include "logging/logger.h"

#include "graphics/backend/d3d12.h"

#include <algorithm>
#include <imgui.h>

namespace Framework::GUI {
    ViewD3D12::ViewD3D12(int id, Graphics::Renderer *graphicsRenderer, Manager *manager): View(id, graphicsRenderer, manager) {}

    ViewD3D12::~ViewD3D12() {
        ReleaseResources();
    }

    GUIError ViewD3D12::Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated) {
        if (gpuAccelerated) {
            // CEF shares D3D11 textures on the accelerated path; consuming them
            // here would require D3D11-on-12 interop, so fall back to OnPaint
            Framework::Logging::GetLogger("Web")->warn("ViewD3D12: GPU-accelerated OSR not supported, falling back to CPU path");
        }
        return View::Init(url, width, height, offsetX, offsetY, false);
    }

    void ViewD3D12::Update() {
        if (!_browser || !_shouldDisplay) {
            return;
        }

        std::scoped_lock lock(_renderMutex);
        View::Update();
    }

    bool ViewD3D12::CreateResources() {
        auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr;
        auto *device  = backend ? backend->GetDevice() : nullptr;
        if (!device) {
            return false;
        }

        // Keep the SRV slot across resizes; the descriptor is rewritten in place
        if (_srvSlot < 0) {
            _srvSlot = backend->AllocateSRVSlot();
            if (_srvSlot < 0) {
                return false;
            }
        }

        // Resize path: the previous texture/upload buffers (and the SRV being
        // rewritten below) may still be referenced by the in-flight command
        // list — the render thread submits one frame ahead. Drain first; if
        // the drain can't be confirmed, keep the old resources and retry on a
        // later frame rather than freeing something the GPU still reads.
        if (_texture && !backend->WaitForGpu()) {
            Framework::Logging::GetLogger("Web")->error("ViewD3D12: GPU drain failed, deferring resource recreation");
            return false;
        }

        _texture.Reset();
        _uploadBuffers.clear();

        D3D12_HEAP_PROPERTIES defaultHeap {};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc {};
        texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width            = static_cast<UINT64>(_width);
        texDesc.Height           = static_cast<UINT>(_height);
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels        = 1;
        texDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_texture)))) {
            Framework::Logging::GetLogger("Web")->error("ViewD3D12: failed to create texture {}x{}", _width, _height);
            return false;
        }
        _textureState = D3D12_RESOURCE_STATE_COPY_DEST;

        _uploadPitch = (static_cast<uint32_t>(_width) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

        D3D12_HEAP_PROPERTIES uploadHeap {};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufDesc {};
        bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width            = static_cast<UINT64>(_uploadPitch) * _height;
        bufDesc.Height           = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels        = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        // Clamped: a zero would leave _uploadBuffers empty and UploadPixels
        // does a modulo by its size
        const auto framesInFlight = static_cast<size_t>(std::max(1, backend->NumFramesInFlight()));
        _uploadBuffers.resize(framesInFlight);

        for (auto &upload : _uploadBuffers) {
            if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload.resource)))) {
                Framework::Logging::GetLogger("Web")->error("ViewD3D12: failed to create upload buffer");
                ReleaseResources();
                return false;
            }

            const D3D12_RANGE noRead {0, 0};
            if (FAILED(upload.resource->Map(0, &noRead, reinterpret_cast<void **>(&upload.mapped)))) {
                Framework::Logging::GetLogger("Web")->error("ViewD3D12: failed to map upload buffer");
                ReleaseResources();
                return false;
            }
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;
        device->CreateShaderResourceView(_texture.Get(), &srvDesc, backend->GetSRVSlotCPUHandle(_srvSlot));
        _srvGpuHandle = backend->GetSRVSlotGPUHandle(_srvSlot);

        _texWidth     = _width;
        _texHeight    = _height;
        _textureReady = false;

        Framework::Logging::GetLogger("Web")->debug("ViewD3D12: resources created {}x{}, srv slot {}", _width, _height, _srvSlot);
        return true;
    }

    void ViewD3D12::ReleaseResources() {
        // In-flight command lists may still reference the texture/upload buffers
        // (the render thread submits one frame ahead) — drain the GPU first.
        // This is the destructor path, so an unconfirmed drain can only be
        // logged: the resources are freed regardless
        if (_texture) {
            if (auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr) {
                if (!backend->WaitForGpu()) {
                    Framework::Logging::GetLogger("Web")->warn("ViewD3D12: GPU drain unconfirmed, freeing resources anyway (teardown)");
                }
            }
        }

        _uploadBuffers.clear();
        _texture.Reset();
        _textureReady = false;

        if (_srvSlot >= 0) {
            if (auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr) {
                backend->FreeSRVSlot(_srvSlot);
            }
            _srvSlot = -1;
        }
    }

    void ViewD3D12::UploadPixels(const std::vector<uint8_t> &pixels) {
        auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr;
        auto *cmdList = backend ? backend->GetGraphicsCommandList() : nullptr;
        if (!cmdList || !_texture || _uploadBuffers.empty()) {
            return;
        }

        const auto frameIdx = backend->GetCurrentFrameIndex() % _uploadBuffers.size();
        uint8_t *dst        = _uploadBuffers[frameIdx].mapped;

        const uint32_t srcPitch = static_cast<uint32_t>(_width) * 4;
        for (int y = 0; y < _height; y++) {
            memcpy(dst + static_cast<size_t>(y) * _uploadPitch, pixels.data() + static_cast<size_t>(y) * srcPitch, srcPitch);
        }

        D3D12_RESOURCE_BARRIER barrier {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = _texture.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        if (_textureState != D3D12_RESOURCE_STATE_COPY_DEST) {
            barrier.Transition.StateBefore = _textureState;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
            cmdList->ResourceBarrier(1, &barrier);
        }

        D3D12_TEXTURE_COPY_LOCATION dstLoc {};
        dstLoc.pResource        = _texture.Get();
        dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc {};
        srcLoc.pResource                          = _uploadBuffers[frameIdx].resource.Get();
        srcLoc.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_B8G8R8A8_UNORM;
        srcLoc.PlacedFootprint.Footprint.Width    = static_cast<UINT>(_width);
        srcLoc.PlacedFootprint.Footprint.Height   = static_cast<UINT>(_height);
        srcLoc.PlacedFootprint.Footprint.Depth    = 1;
        srcLoc.PlacedFootprint.Footprint.RowPitch = _uploadPitch;

        cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        cmdList->ResourceBarrier(1, &barrier);
        _textureState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        _textureReady = true;
    }

    void ViewD3D12::Render() {
        if (!_browser || !_shouldDisplay) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr;
        if (!backend) {
            return;
        }

        auto *renderHandler = GetRenderHandler();
        if (!renderHandler) {
            return;
        }

        // Held for the whole read: OnPaint (game thread, inside the CEF pump)
        // reallocates the buffer on resize
        const auto pixelLock = renderHandler->LockPixels();

        const auto &pixels = renderHandler->GetPixelData();
        if (pixels.empty()) {
            return;
        }

        if (!_texture || _texWidth != _width || _texHeight != _height) {
            if (!CreateResources()) {
                return;
            }
        }

        if (renderHandler->IsPixelDataDirty() || !_textureReady) {
            // The pixel buffer can briefly lag the view size during a resize
            if (pixels.size() >= static_cast<size_t>(_width) * _height * 4) {
                UploadPixels(pixels);
                renderHandler->ClearPixelDataDirty();
            }
        }
    }

    void ViewD3D12::SubmitImGuiDraw() {
        std::scoped_lock lock(_renderMutex);

        if (!_shouldDisplay || !_textureReady || _srvSlot < 0) {
            return;
        }

        auto *drawList = ImGui::GetBackgroundDrawList();
        drawList->AddImage(static_cast<ImTextureID>(_srvGpuHandle.ptr), ImVec2(static_cast<float>(_x), static_cast<float>(_y)),
            ImVec2(static_cast<float>(_x + _width), static_cast<float>(_y + _height)));
    }

    std::string ViewD3D12::GetDebugString() const {
        auto *renderHandler = _renderHandler.get();
        size_t pixelBytes   = 0;
        if (renderHandler) {
            const auto pixelLock = renderHandler->LockPixels();
            pixelBytes           = renderHandler->GetPixelData().size();
        }
        return fmt::format("d3d12 display={} texReady={} srv={} tex={}x{} pixels={} dirty={}", _shouldDisplay, _textureReady, _srvSlot, _texWidth, _texHeight, pixelBytes,
            renderHandler ? renderHandler->IsPixelDataDirty() : false);
    }
} // namespace Framework::GUI
