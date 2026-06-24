/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "render_handler.h"

namespace Framework::GUI::CEF {
    void RenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) {
        rect = CefRect(0, 0, _width, _height);
    }

    void RenderHandler::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const CefAcceleratedPaintInfo &info) {
        if (!_device || type != PET_VIEW) {
            return;
        }

        std::lock_guard<std::mutex> lock(_textureMutex);

        HANDLE textureHandle = info.shared_texture_handle;
        if (!textureHandle) {
            return;
        }

        // Only re-open the shared resource if the handle changed
        if (textureHandle != _sharedHandle) {
            _sharedTexture.Reset();

            Microsoft::WRL::ComPtr<ID3D11Texture2D> sharedTex;
            HRESULT hr = _device->OpenSharedResource(textureHandle, IID_PPV_ARGS(&sharedTex));
            if (SUCCEEDED(hr)) {
                _sharedTexture = sharedTex;
                _sharedHandle  = textureHandle;
            }
        }
    }

    void RenderHandler::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void *buffer, int width, int height) {
        if (type != PET_VIEW) {
            return;
        }

        // resize below can reallocate the buffer while a view reads it
        std::lock_guard<std::mutex> lock(_pixelMutex);

        const size_t size = width * height * 4;
        if (_pixelData.size() != size) {
            _pixelData.resize(size);
        }
        std::memcpy(_pixelData.data(), buffer, size);
        _pixelDataDirty = true;
    }
} // namespace Framework::GUI::CEF
