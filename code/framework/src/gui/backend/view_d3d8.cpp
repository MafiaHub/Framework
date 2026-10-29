/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "view_d3d8.h"

#include "graphics/backend/d3d8.h"

namespace Framework::GUI {
    namespace D3D8 = Graphics::D3D8;

    ViewD3D8::ViewD3D8(int id, Graphics::Renderer *graphicsRenderer, Manager *manager): View(id, graphicsRenderer, manager) {}

    Utils::Result<void, Framework::Error> ViewD3D8::Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated) {
        (void)gpuAccelerated;
        return View::Init(url, width, height, offsetX, offsetY, false);
    }

    bool ViewD3D8::UploadPixels(D3D8::Device *device) {
        auto *renderHandler = GetRenderHandler();
        if (!renderHandler) {
            return false;
        }

        // held for the whole read; OnPaint reallocates on resize
        const auto pixelLock = renderHandler->LockPixels();
        const auto &pixels   = renderHandler->GetPixelData();

        // resize in flight: keep the last frame until CEF repaints at the current size
        if (pixels.size() != static_cast<size_t>(_width) * static_cast<size_t>(_height) * 4) {
            return _surface.HasContent(_width, _height);
        }

        bool fullUpload = false;
        if (!_surface.Prepare(device, _width, _height, _id, fullUpload)) {
            return false;
        }

        if (!fullUpload && !renderHandler->IsPixelDataDirty()) {
            return true;
        }

        RECT region {0, 0, _width, _height};
        if (!fullUpload) {
            const CefRect &dirty = renderHandler->GetDirtyBounds();
            if (dirty.IsEmpty()) {
                renderHandler->ClearPixelDataDirty();
                return true;
            }
            region = {dirty.x, dirty.y, dirty.x + dirty.width, dirty.y + dirty.height};
        }

        if (!_surface.Upload(pixels.data(), static_cast<size_t>(_width) * 4, _width, _height, region)) {
            // keep the dirty flag so the copy retries next frame
            return !fullUpload;
        }

        renderHandler->ClearPixelDataDirty();
        return true;
    }

    void ViewD3D8::Render() {
        if (!_browser || (!IsOnScreen() && !_offscreen)) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        auto *backend = _graphicsRenderer->GetD3D8Backend();
        if (!backend || !backend->IsDeviceReady()) {
            return;
        }
        auto *device = backend->GetDevice();

        if (!UploadPixels(device) || !_surface.GetTexture()) {
            return;
        }

        if (!IsOnScreen()) {
            return; // offscreen: painted, not composited
        }

        _surface.Draw(device, _x, _y, _width, _height);
    }
} // namespace Framework::GUI
