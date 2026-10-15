/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "view_d3d12.h"
#include "gui/cef/render_handler.h"
#include "logging/logger.h"
#include "utils/profiler.h"

#include "graphics/backend/d3d12.h"
#include "graphics/backend/d3d12_ui_compositor.h"

#include <algorithm>
#include <imgui.h>
#ifdef FW_IMGUI_DX12
#include <imgui_impl_dx12.h>
#endif

namespace Framework::GUI {
    namespace {
#ifdef FW_IMGUI_DX12
        struct ImageDraw {
            Graphics::D3D12Backend *backend;
            D3D12_GPU_DESCRIPTOR_HANDLE texture;
            float x, y, width, height;
        };

        void DrawPremultipliedImage(const ImDrawList *, const ImDrawCmd *command) {
            // ImGui owns a copy of this data; it never points back into the view.
            const auto &image = *static_cast<const ImageDraw *>(command->UserCallbackData);
            const auto *data  = ImGui::GetDrawData();
            auto *state       = static_cast<ImGui_ImplDX12_RenderState *>(ImGui::GetPlatformIO().Renderer_RenderState);
            const D3D12_RECT scissor {static_cast<LONG>(std::max(0.0f, (command->ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x)), static_cast<LONG>(std::max(0.0f, (command->ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y)),
                static_cast<LONG>((command->ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x), static_cast<LONG>((command->ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y)};
            if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) {
                return;
            }
            state->CommandList->RSSetScissorRects(1, &scissor);
            const float rect[4] {(image.x - data->DisplayPos.x) * 2 / data->DisplaySize.x - 1, 1 - (image.y - data->DisplayPos.y) * 2 / data->DisplaySize.y, image.width * 2 / data->DisplaySize.x, -image.height * 2 / data->DisplaySize.y};
            const auto format    = Graphics::D3D12UICompositor::RenderFormat(image.backend->GetCurrentBackBuffer()->GetDesc().Format);
            const HRESULT result = image.backend->GetPremultipliedImage().Draw(state->Device, state->CommandList, format, image.texture, rect);
            if (FAILED(result)) {
                Framework::Logging::GetLogger("Web")->error("DX12 premultiplied image pipeline failed: {:#x}", static_cast<unsigned>(result));
            }
        }
#endif

        constexpr DWORD kSharedCopyTimeoutMs = 1000;

        void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER barrier {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter  = after;
            list->ResourceBarrier(1, &barrier);
        }

        // Rests in COMMON between submissions, so the game's list can promote it to
        // COPY_SOURCE implicitly and it decays back once that list has run.
        template <typename Texture>
        bool EnsureTexture(ID3D12Device *device, Texture &texture, UINT width, UINT height) {
            if (texture.resource && texture.width == width && texture.height == height) {
                return true;
            }
            texture.resource.Reset();

            D3D12_HEAP_PROPERTIES heap {};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC desc {};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width            = width;
            desc.Height           = height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture.resource)))) {
                Framework::Logging::GetLogger("Web")->error("ViewD3D12: failed to create shared copy texture {}x{}", width, height);
                return false;
            }
            texture.width  = width;
            texture.height = height;
            return true;
        }
    } // namespace

    ViewD3D12::ViewD3D12(int id, Graphics::Renderer *graphicsRenderer, Manager *manager): View(id, graphicsRenderer, manager) {}

    ViewD3D12::~ViewD3D12() {
        if (auto *renderHandler = GetRenderHandler()) {
            renderHandler->SetAcceleratedPaintCallback(nullptr);
        }
        ReleaseResources();
        ReleaseSharedCopy();
    }

    Utils::Result<void, Framework::Error> ViewD3D12::Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated) {
        if (gpuAccelerated) {
            auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr;
            if (!backend || !backend->GetDevice() || !CreateSharedCopy(backend->GetDevice())) {
                Framework::Logging::GetLogger("Web")->warn("ViewD3D12: shared texture copy unavailable, falling back to CPU path");
                ReleaseSharedCopy();
                gpuAccelerated = false;
            }
        }

        if (auto result = View::Init(url, width, height, offsetX, offsetY, gpuAccelerated); !result) {
            return result;
        }

        if (gpuAccelerated) {
            GetRenderHandler()->SetAcceleratedPaintCallback([this](CefRenderHandler::PaintElementType type, HANDLE handle) {
                CopySharedFrame(type, handle);
            });
        }
        return {};
    }

    bool ViewD3D12::CreateSharedCopy(ID3D12Device *device) {
        D3D12_COMMAND_QUEUE_DESC queueDesc {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_sharedCopy.queue)))) {
            return false;
        }
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_sharedCopy.allocator)))) {
            return false;
        }
        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _sharedCopy.allocator.Get(), nullptr, IID_PPV_ARGS(&_sharedCopy.list))) || FAILED(_sharedCopy.list->Close())) {
            return false;
        }
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_sharedCopy.fence)))) {
            return false;
        }
        _sharedCopy.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        return _sharedCopy.event != nullptr;
    }

    void ViewD3D12::ReleaseSharedCopy() {
        // Every copy is waited for before CopySharedFrame returns, so the queue is
        // idle; staging textures may still be read by a game frame in flight.
        if (!_staging.empty()) {
            if (auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr) {
                if (!backend->WaitForGpu()) {
                    Framework::Logging::GetLogger("Web")->warn("ViewD3D12: GPU drain unconfirmed, freeing shared copies anyway (teardown)");
                }
            }
        }

        _staging.clear();
        _sharedPage  = {};
        _sharedPopup = {};
        _sharedCopy.list.Reset();
        _sharedCopy.allocator.Reset();
        _sharedCopy.queue.Reset();
        _sharedCopy.fence.Reset();
        if (_sharedCopy.event) {
            CloseHandle(_sharedCopy.event);
            _sharedCopy.event = nullptr;
        }
    }

    void ViewD3D12::CopySharedFrame(CefRenderHandler::PaintElementType type, HANDLE handle) {
        FW_PROFILE_SCOPE_N("Cef::OnAcceleratedPaint");

        auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr;
        auto *device  = backend ? backend->GetDevice() : nullptr;
        if (!device) {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12Resource> source;
        if (FAILED(device->OpenSharedHandle(handle, IID_PPV_ARGS(&source)))) {
            // A texture from another adapter never opens; see force-high-performance-gpu
            Framework::Logging::GetLogger("Web")->error("ViewD3D12: could not open CEF's shared texture");
            return;
        }
        const auto sourceDesc = source->GetDesc();

        auto &target = type == PET_POPUP ? _sharedPopup : _sharedPage;
        if (!EnsureTexture(device, target, static_cast<UINT>(sourceDesc.Width), sourceDesc.Height) || !_sharedPage.resource) {
            return;
        }

        // Never the one the render thread may still pick up, and one per frame in
        // flight past that, so the oldest has normally been read already.
        if (_staging.empty()) {
            _staging.resize(static_cast<size_t>(std::max(1, backend->NumFramesInFlight())) + 1);
        }
        int slot         = 0;
        UINT64 readFence = 0;
        {
            std::scoped_lock lock(_sharedMutex);
            slot      = (_sharedLatest + 1) % static_cast<int>(_staging.size());
            readFence = _staging[slot].readFence;
        }
        auto &staging = _staging[slot].texture;

        auto *frameFence = backend->GetFrameFence();
        if (staging.width != _sharedPage.width || staging.height != _sharedPage.height) {
            // Freed on the CPU, so the frame reading it has to have run first
            if (frameFence->GetCompletedValue() < readFence && SUCCEEDED(frameFence->SetEventOnCompletion(readFence, _sharedCopy.event))) {
                WaitForSingleObject(_sharedCopy.event, kSharedCopyTimeoutMs);
            }
            if (!EnsureTexture(device, staging, _sharedPage.width, _sharedPage.height)) {
                return;
            }
        }

        auto *list = _sharedCopy.list.Get();
        if (FAILED(_sharedCopy.allocator->Reset()) || FAILED(list->Reset(_sharedCopy.allocator.Get(), nullptr))) {
            Framework::Logging::GetLogger("Web")->error("ViewD3D12: shared copy list reset failed");
            return;
        }

        CefRect popupRect;
        const bool popup = _sharedPopup.resource && GetRenderHandler()->GetPopupRect(popupRect);

        Transition(list, target.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(target.resource.Get(), source.Get());
        Transition(list, target.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (type == PET_POPUP) {
            Transition(list, _sharedPage.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
        else if (popup) {
            Transition(list, _sharedPopup.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }

        // The page, then the popup over it where it sits
        Transition(list, staging.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyResource(staging.resource.Get(), _sharedPage.resource.Get());
        if (popup) {
            const int x0 = std::max(0, popupRect.x);
            const int y0 = std::max(0, popupRect.y);
            const int x1 = std::min(static_cast<int>(staging.width), popupRect.x + static_cast<int>(_sharedPopup.width));
            const int y1 = std::min(static_cast<int>(staging.height), popupRect.y + static_cast<int>(_sharedPopup.height));
            if (x1 > x0 && y1 > y0) {
                D3D12_TEXTURE_COPY_LOCATION dst {};
                dst.pResource = staging.resource.Get();
                dst.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION src {};
                src.pResource = _sharedPopup.resource.Get();
                src.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                const D3D12_BOX box {static_cast<UINT>(x0 - popupRect.x), static_cast<UINT>(y0 - popupRect.y), 0, static_cast<UINT>(x1 - popupRect.x), static_cast<UINT>(y1 - popupRect.y), 1};
                list->CopyTextureRegion(&dst, static_cast<UINT>(x0), static_cast<UINT>(y0), 0, &src, &box);
            }
        }

        Transition(list, staging.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        Transition(list, _sharedPage.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        if (popup || type == PET_POPUP) {
            Transition(list, _sharedPopup.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        }

        if (FAILED(list->Close())) {
            Framework::Logging::GetLogger("Web")->error("ViewD3D12: shared copy list close failed");
            return;
        }

        // The game frame that last copied from this staging texture has to be done
        _sharedCopy.queue->Wait(frameFence, readFence);
        ID3D12CommandList *lists[] {list};
        _sharedCopy.queue->ExecuteCommandLists(1, lists);
        _sharedCopy.queue->Signal(_sharedCopy.fence.Get(), ++_sharedCopy.fenceValue);

        // CEF hands the source back to its pool as soon as we return
        {
            FW_PROFILE_SCOPE_N("Cef::OnAcceleratedPaint::Wait");
            if (_sharedCopy.fence->GetCompletedValue() < _sharedCopy.fenceValue) {
                if (FAILED(_sharedCopy.fence->SetEventOnCompletion(_sharedCopy.fenceValue, _sharedCopy.event)) || WaitForSingleObject(_sharedCopy.event, kSharedCopyTimeoutMs) != WAIT_OBJECT_0) {
                    Framework::Logging::GetLogger("Web")->error("ViewD3D12: shared texture copy timed out");
                    return;
                }
            }
        }

        if (!_sharedLogged) {
            _sharedLogged = true;
            Framework::Logging::GetLogger("Web")->debug("ViewD3D12: accelerated paint active ({}x{})", staging.width, staging.height);
        }

        std::scoped_lock lock(_sharedMutex);
        _sharedLatest = slot;
        _sharedDirty  = true;
    }

    void ViewD3D12::RenderShared(Graphics::D3D12Backend *backend) {
        auto *cmdList = backend->GetGraphicsCommandList();
        // Begin() failed this frame, so the list never executes
        if (!cmdList || !backend->GetCurrentBackBuffer()) {
            return;
        }

        std::scoped_lock lock(_sharedMutex);
        if (_sharedLatest < 0) {
            return;
        }

        if (!_texture || _texWidth != _width || _texHeight != _height) {
            if (!CreateResources(false)) {
                return;
            }
            _sharedDirty = true;
        }
        if (!_sharedDirty) {
            return;
        }

        auto &staging = _staging[_sharedLatest];
        if (_textureState != D3D12_RESOURCE_STATE_COPY_DEST) {
            Transition(cmdList, _texture.Get(), _textureState, D3D12_RESOURCE_STATE_COPY_DEST);
        }

        // The staging texture can briefly lag the view size during a resize
        D3D12_TEXTURE_COPY_LOCATION dst {};
        dst.pResource = _texture.Get();
        dst.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src {};
        src.pResource = staging.texture.resource.Get();
        src.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        const D3D12_BOX box {0, 0, 0, std::min(staging.texture.width, static_cast<UINT>(_texWidth)), std::min(staging.texture.height, static_cast<UINT>(_texHeight)), 1};
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);

        Transition(cmdList, _texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        _textureState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        staging.readFence = backend->GetPendingFrameFenceValue();
        _sharedDirty      = false;
        _textureReady     = true;
    }

    bool ViewD3D12::CreateResources(bool uploadBuffers) {
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

        // old texture/buffers may still be in the in-flight command list; drain
        // first, and if unconfirmed keep them and retry later
        if (_texture && !backend->WaitForGpu()) {
            Framework::Logging::GetLogger("Web")->error("ViewD3D12: GPU drain failed, deferring resource recreation");
            return false;
        }

        _textureReady = false;
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

        // The GPU path copies texture to texture and never uploads
        if (uploadBuffers) {
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

            // clamp to >=1: UploadPixels does a modulo by _uploadBuffers.size()
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
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
        srvDesc.Format                  = DXGI_FORMAT_B8G8R8A8_UNORM;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;
        device->CreateShaderResourceView(_texture.Get(), &srvDesc, backend->GetSRVSlotCPUHandle(_srvSlot));
        _srvGpuHandle = backend->GetSRVSlotGPUHandle(_srvSlot);

        _texWidth  = _width;
        _texHeight = _height;

        Framework::Logging::GetLogger("Web")->debug("ViewD3D12: resources created {}x{}, srv slot {}", _width, _height, _srvSlot);
        return true;
    }

    void ViewD3D12::ReleaseResources() {
        // drain first — in-flight lists may still reference these (destructor: only log on fail)
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

    bool ViewD3D12::UploadPixels(const std::vector<uint8_t> &pixels) {
        auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr;
        auto *cmdList = backend ? backend->GetGraphicsCommandList() : nullptr;
        if (!cmdList || !_texture || _uploadBuffers.empty()) {
            return false;
        }

        const auto frameIdx = backend->GetCurrentFrameIndex() % _uploadBuffers.size();
        uint8_t *dst        = _uploadBuffers[frameIdx].mapped;

        const uint32_t srcPitch = static_cast<uint32_t>(_width) * 4;
        for (int y = 0; y < _height; y++) {
            // Upload memory may be write-combined: only write to it. CEF's
            // premultiplied alpha is handled by the image draw's GPU blend state.
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
        return true;
    }

    void ViewD3D12::Render() {
        if (!_browser || !IsOnScreen()) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        auto *backend = _graphicsRenderer ? _graphicsRenderer->GetD3D12Backend() : nullptr;
        if (!backend) {
            return;
        }

        if (_gpuAccelerated) {
            RenderShared(backend);
            return;
        }

        auto *renderHandler = GetRenderHandler();
        if (!renderHandler) {
            return;
        }

        // held for the whole read; OnPaint reallocates the buffer on resize
        const auto pixelLock = renderHandler->LockPixels();

        const auto &pixels = renderHandler->GetPixelData();
        if (pixels.empty()) {
            return;
        }

        if (!_texture || _texWidth != _width || _texHeight != _height) {
            if (!CreateResources(true)) {
                return;
            }
        }

        if (renderHandler->IsPixelDataDirty() || !_textureReady) {
            // The pixel buffer can briefly lag the view size during a resize
            if (pixels.size() >= static_cast<size_t>(_width) * _height * 4) {
                if (UploadPixels(pixels)) {
                    renderHandler->ClearPixelDataDirty();
                }
            }
        }
    }

    void ViewD3D12::SubmitImGuiDraw() {
        std::scoped_lock lock(_renderMutex);

        if (!IsOnScreen() || !_textureReady || _srvSlot < 0) {
            return;
        }

        // Keep the web draw in the background list's ordering, then restore
        // ImGui's straight-alpha pipeline for the following widgets.
#ifdef FW_IMGUI_DX12
        auto *drawList = ImGui::GetBackgroundDrawList();
        ImageDraw image {_graphicsRenderer->GetD3D12Backend(), _srvGpuHandle, static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_width), static_cast<float>(_height)};
        drawList->AddCallback(DrawPremultipliedImage, &image, sizeof(image));
        drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState);
#endif
    }
} // namespace Framework::GUI
