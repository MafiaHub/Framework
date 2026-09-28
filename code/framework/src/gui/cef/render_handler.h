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

        // OnPaint (CEF pump thread) writes, views read on the render thread —
        // both under LockPixels(). _pixelData is a persistent accumulator: OnPaint
        // only refreshes the dirty rects, so its dimensions must be tracked.
        std::mutex _pixelMutex;
        std::vector<uint8_t> _pixelData;
        int _pixelWidth      = 0;
        int _pixelHeight     = 0;
        bool _pixelDataDirty = false;

        // A popup widget (a <select> list, a date picker) is painted apart from the
        // page. _popupRect is where it sits, in view coordinates. Written on the CEF
        // UI thread, read by the render thread.
        std::mutex _popupMutex;
        bool _popupVisible = false;
        CefRect _popupRect;

        // Software path, CEF UI thread only: the popup's own pixels, and the page
        // pixels it covers in _pixelData so they can be put back when it moves or
        // closes. _stampedRect is empty while nothing is drawn over the page.
        std::vector<uint8_t> _popupPixels;
        int _popupPixelWidth  = 0;
        int _popupPixelHeight = 0;
        std::vector<uint8_t> _popupUnderlay;
        CefRect _stampedRect;

        // Accelerated path: the popup arrives as its own shared texture.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> _popupSharedTexture;
        HANDLE _popupSharedHandle = nullptr;

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
        void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
        void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect &rect) override;

        // Where the popup is drawn, in view coordinates; false while none is open.
        bool GetPopupRect(CefRect &rect);

        [[nodiscard]] std::lock_guard<std::mutex> LockTexture() {
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

        ID3D11Texture2D *GetPopupSharedTexture() const {
            return _popupSharedTexture.Get();
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

      private:
        void OpenSharedTexture(HANDLE handle, Microsoft::WRL::ComPtr<ID3D11Texture2D> &texture, HANDLE &openedHandle);
        void StampPopup();
        void UnstampPopup();

        IMPLEMENT_REFCOUNTING(RenderHandler);
    };
} // namespace Framework::GUI::CEF
