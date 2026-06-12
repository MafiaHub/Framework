/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <d3d11.h>
#include <mutex>
#include <vector>
#include <wrl/client.h>

#include "include/cef_render_handler.h"

namespace Framework::GUI::CEF {
    class RenderHandler final: public CefRenderHandler {
      private:
        int _width  = 0;
        int _height = 0;

        std::mutex _textureMutex;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> _sharedTexture;
        HANDLE _sharedHandle = nullptr;

        // CPU fallback. OnPaint writes from the thread pumping CEF (the game
        // thread); views read from the render thread — hold LockPixels() for
        // the whole read, GetPixelData() returns a reference
        std::mutex _pixelMutex;
        std::vector<uint8_t> _pixelData;
        bool _pixelDataDirty = false;

        ID3D11Device *_device = nullptr;

      public:
        void SetDimensions(int width, int height) {
            _width  = width;
            _height = height;
        }

        void SetD3D11Device(ID3D11Device *device) {
            _device = device;
        }

        void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override;
        void OnAcceleratedPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const CefAcceleratedPaintInfo &info) override;
        void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void *buffer, int width, int height) override;

        std::lock_guard<std::mutex> LockTexture() {
            return std::lock_guard<std::mutex>(_textureMutex);
        }

        [[nodiscard]] std::lock_guard<std::mutex> LockPixels() {
            return std::lock_guard<std::mutex>(_pixelMutex);
        }

        ID3D11Texture2D *GetSharedTexture() const {
            return _sharedTexture.Get();
        }

        HANDLE GetSharedHandle() const {
            return _sharedHandle;
        }

        const std::vector<uint8_t> &GetPixelData() const {
            return _pixelData;
        }

        bool IsPixelDataDirty() const {
            return _pixelDataDirty;
        }

        void ClearPixelDataDirty() {
            _pixelDataDirty = false;
        }

        IMPLEMENT_REFCOUNTING(RenderHandler);
    };
} // namespace Framework::GUI::CEF
