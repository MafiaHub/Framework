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

        // Copies a width*height block between two BGRA surfaces of the given strides.
        void CopyBlock(uint8_t *dst, int dstStride, int dstX, int dstY, const uint8_t *src, int srcStride, int srcX, int srcY, int width, int height) {
            const size_t spanBytes = static_cast<size_t>(width) * 4;
            for (int y = 0; y < height; ++y) {
                const size_t dstOffset = (static_cast<size_t>(dstY + y) * dstStride + dstX) * 4;
                const size_t srcOffset = (static_cast<size_t>(srcY + y) * srcStride + srcX) * 4;
                std::memcpy(dst + dstOffset, src + srcOffset, spanBytes);
            }
        }
    } // namespace

    void RenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) {
        rect = CefRect(0, 0, _width, _height);
    }

    void RenderHandler::OnAcceleratedPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const CefAcceleratedPaintInfo &info) {
        if (!_device) {
            return;
        }

        std::lock_guard<std::mutex> lock(_textureMutex);

        HANDLE textureHandle = info.shared_texture_handle;
        if (!textureHandle) {
            return;
        }

        if (type == PET_POPUP) {
            OpenSharedTexture(textureHandle, _popupSharedTexture, _popupSharedHandle);
        }
        else {
            OpenSharedTexture(textureHandle, _sharedTexture, _sharedHandle);
        }
    }

    void RenderHandler::OpenSharedTexture(HANDLE handle, Microsoft::WRL::ComPtr<ID3D11Texture2D> &texture, HANDLE &openedHandle) {
        // Only re-open the shared resource if the handle changed
        if (handle == openedHandle) {
            return;
        }

        texture.Reset();
        openedHandle = nullptr;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> sharedTex;
        HRESULT hr = _device->OpenSharedResource(handle, IID_PPV_ARGS(&sharedTex));
        if (SUCCEEDED(hr)) {
            texture      = sharedTex;
            openedHandle = handle;
        }
    }

    void RenderHandler::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void *buffer, int width, int height) {
        FW_PROFILE_SCOPE_N("Cef::OnPaint");

        // resize below can reallocate the buffer while a view reads it
        std::unique_lock<std::mutex> lock;
        {
            FW_PROFILE_SCOPE_N("Cef::OnPaint::LockWait");
            lock = std::unique_lock<std::mutex>(_pixelMutex);
        }

        if (type == PET_POPUP) {
            const auto *pixels = static_cast<const uint8_t *>(buffer);
            _popupPixels.assign(pixels, pixels + static_cast<size_t>(width) * 4 * static_cast<size_t>(height));
            _popupPixelWidth  = width;
            _popupPixelHeight = height;

            UnstampPopup();
            StampPopup();
            _pixelDataDirty = true;
            return;
        }

        // The page is painted underneath the popup, so lift the popup off first and
        // lay it back over whatever the page now shows there.
        UnstampPopup();

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

        StampPopup();

        // No damage means no upload: the views re-push the whole surface on dirty.
        if (copied > 0) {
            _pixelDataDirty = true;
        }

        FW_PROFILE_PLOT("cef.paint.bytes", static_cast<int64_t>(copied));
        FW_PROFILE_PLOT("cef.paint.surfaceBytes", static_cast<int64_t>(size));
    }

    void RenderHandler::OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) {
        {
            std::lock_guard<std::mutex> popupLock(_popupMutex);
            _popupVisible = show;
            if (!show) {
                _popupRect = CefRect();
            }
        }

        // Nothing to draw until OnPopupSize places it and OnPaint paints it
        if (show) {
            return;
        }

        {
            std::lock_guard<std::mutex> pixelLock(_pixelMutex);
            UnstampPopup();
            _popupPixels.clear();
            _popupPixelWidth  = 0;
            _popupPixelHeight = 0;
            _pixelDataDirty   = true;
        }

        std::lock_guard<std::mutex> textureLock(_textureMutex);
        _popupSharedTexture.Reset();
        _popupSharedHandle = nullptr;
    }

    void RenderHandler::OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect &rect) {
        // Chromium already keeps the popup inside the view (it flips a list upward when
        // there is no room below), so the rect is used as given.
        std::lock_guard<std::mutex> pixelLock(_pixelMutex);
        UnstampPopup();

        {
            std::lock_guard<std::mutex> popupLock(_popupMutex);
            _popupRect = rect;
        }

        // A move keeps the painted pixels; a resize waits for CEF to repaint at the new size.
        if (_popupPixelWidth == rect.width && _popupPixelHeight == rect.height) {
            StampPopup();
        }
        else {
            _popupPixels.clear();
            _popupPixelWidth  = 0;
            _popupPixelHeight = 0;
        }
        _pixelDataDirty = true;
    }

    bool RenderHandler::GetPopupRect(CefRect &rect) {
        std::lock_guard<std::mutex> popupLock(_popupMutex);
        if (!_popupVisible || _popupRect.IsEmpty()) {
            return false;
        }
        rect = _popupRect;
        return true;
    }

    void RenderHandler::StampPopup() {
        if (!_popupVisible || _popupPixels.empty()) {
            return;
        }

        // The part of the painted popup that falls inside the page surface
        const int x0 = (std::max)(0, _popupRect.x);
        const int y0 = (std::max)(0, _popupRect.y);
        const int x1 = (std::min)(_pixelWidth, _popupRect.x + _popupPixelWidth);
        const int y1 = (std::min)(_pixelHeight, _popupRect.y + _popupPixelHeight);
        if (x1 <= x0 || y1 <= y0) {
            return;
        }

        _stampedRect = CefRect(x0, y0, x1 - x0, y1 - y0);
        _popupUnderlay.resize(static_cast<size_t>(_stampedRect.width) * 4 * static_cast<size_t>(_stampedRect.height));
        CopyBlock(_popupUnderlay.data(), _stampedRect.width, 0, 0, _pixelData.data(), _pixelWidth, x0, y0, _stampedRect.width, _stampedRect.height);
        CopyBlock(_pixelData.data(), _pixelWidth, x0, y0, _popupPixels.data(), _popupPixelWidth, x0 - _popupRect.x, y0 - _popupRect.y, _stampedRect.width, _stampedRect.height);
    }

    void RenderHandler::UnstampPopup() {
        if (_stampedRect.IsEmpty()) {
            return;
        }

        CopyBlock(_pixelData.data(), _pixelWidth, _stampedRect.x, _stampedRect.y, _popupUnderlay.data(), _stampedRect.width, 0, 0, _stampedRect.width, _stampedRect.height);
        _stampedRect = CefRect();
    }
} // namespace Framework::GUI::CEF
