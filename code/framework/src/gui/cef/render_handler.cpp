/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "render_handler.h"

#include <utils/profiler.h>

#include <algorithm>
#include <cstring>

namespace Framework::GUI::CEF {
    namespace {
        // Refresh only what CEF marked dirty. dst and src share a layout, so a rect sits
        // at the same offset in both. Returns the bytes copied.
        size_t CopyDirtyRects(const CefRenderHandler::RectList &dirtyRects, uint8_t *dst, const uint8_t *src, int width, int height) {
            const size_t rowBytes = static_cast<size_t>(width) * 4;

            size_t copied = 0;
            for (const auto &rect : dirtyRects) {
                const int x0 = (std::max)(0, rect.x);
                const int y0 = (std::max)(0, rect.y);
                const int x1 = (std::min)(width, rect.x + rect.width);
                const int y1 = (std::min)(height, rect.y + rect.height);
                if (x1 <= x0 || y1 <= y0) {
                    continue;
                }

                const size_t rows      = static_cast<size_t>(y1 - y0);
                const size_t spanBytes = static_cast<size_t>(x1 - x0) * 4;
                copied += spanBytes * rows;

                // Full-width rects are contiguous, so they go in one memcpy
                if (spanBytes == rowBytes) {
                    const size_t offset = static_cast<size_t>(y0) * rowBytes;
                    std::memcpy(dst + offset, src + offset, rowBytes * rows);
                    continue;
                }

                for (int y = y0; y < y1; ++y) {
                    const size_t offset = static_cast<size_t>(y) * rowBytes + static_cast<size_t>(x0) * 4;
                    std::memcpy(dst + offset, src + offset, spanBytes);
                }
            }

            return copied;
        }
    } // namespace

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

        FW_PROFILE_SCOPE_N("Cef::OnPaint");

        // resize below can reallocate the buffer while a view reads it
        std::unique_lock<std::mutex> lock;
        {
            FW_PROFILE_SCOPE_N("Cef::OnPaint::LockWait");
            lock = std::unique_lock<std::mutex>(_pixelMutex);
        }

        const size_t rowBytes = static_cast<size_t>(width) * 4;
        const size_t size     = rowBytes * static_cast<size_t>(height);
        const auto *src       = static_cast<const uint8_t *>(buffer);

        // A reshape invalidates the whole accumulator; anything else only needs the
        // rects CEF marked, which is a few hundred KB of an ~8 MB surface.
        const bool reshaped = _pixelData.size() != size || _pixelWidth != width || _pixelHeight != height;
        if (reshaped) {
            _pixelData.resize(size);
            _pixelWidth  = width;
            _pixelHeight = height;
        }

        size_t copied = 0;
        {
            FW_PROFILE_SCOPE_N("Cef::OnPaint::Copy");
            if (reshaped) {
                std::memcpy(_pixelData.data(), src, size);
                copied = size;
            }
            else {
                copied = CopyDirtyRects(dirtyRects, _pixelData.data(), src, width, height);
            }
        }

        // No damage means no upload: the views re-push the whole surface on dirty.
        if (copied > 0) {
            _pixelDataDirty = true;
        }

        FW_PROFILE_PLOT("cef.paint.bytes", static_cast<int64_t>(copied));
        FW_PROFILE_PLOT("cef.paint.surfaceBytes", static_cast<int64_t>(size));
    }
} // namespace Framework::GUI::CEF
