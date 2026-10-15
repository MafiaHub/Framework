/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <d3d11_1.h>
#include <array>
#include <dxgi1_2.h>
#include <functional>
#include <mutex>
#include <vector>
#include <wrl/client.h>

#include "include/cef_render_handler.h"

namespace Framework::GUI::CEF {
    class RenderHandler final: public CefRenderHandler {
      public:
        // Takes the accelerated paint in place of the D3D11 device. The handle is
        // only valid for the duration of the call.
        using AcceleratedPaintCallback = std::function<void(PaintElementType, HANDLE)>;

      private:
        int _width  = 0;
        int _height = 0;

        // Accelerated path. CEF's texture goes back to its pool when the callback
        // returns, so it is copied out on the pump thread through a device of our
        // own (the game's immediate context belongs to its render thread) into a
        // ring of textures both devices share. A buffer is only rewritten once the
        // game's event query says the draw that read it has run. All of it is
        // guarded by _textureMutex.
        struct SharedBuffer {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> owned;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> game;
            Microsoft::WRL::ComPtr<ID3D11Query> read;
            bool reading = false;
        };
        struct SharedCopy {
            std::array<SharedBuffer, 3> buffers;
            int latest  = -1;
            UINT width  = 0;
            UINT height = 0;
        };

        std::mutex _textureMutex;
        SharedCopy _sharedPage;

        // OnPaint (CEF pump thread) writes, views read on the render thread —
        // both under LockPixels(). _pixelData is a persistent accumulator: OnPaint
        // only refreshes the dirty rects, so its dimensions must be tracked.
        std::mutex _pixelMutex;
        std::vector<uint8_t> _pixelData;
        int _pixelWidth      = 0;
        int _pixelHeight     = 0;
        bool _pixelDataDirty = false;
        // Bounding box of what changed since the last ClearPixelDataDirty()
        CefRect _pixelDirtyRect;

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
        SharedCopy _sharedPopup;

        ID3D11Device *_device = nullptr;
        Microsoft::WRL::ComPtr<ID3D11Device1> _copyDevice;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _copyContext;
        Microsoft::WRL::ComPtr<ID3D11Query> _copyDone;
        bool _copyFailed = false;
        bool _copyLogged = false;
        // Held across the call, so clearing the callback waits out one in progress
        std::mutex _acceleratedPaintMutex;
        AcceleratedPaintCallback _acceleratedPaintCallback;

      public:
        void SetDimensions(int width, int height) {
            _width  = width;
            _height = height;
        }

        void SetD3D11Device(ID3D11Device *device) {
            _device = device;
        }

        void SetAcceleratedPaintCallback(AcceleratedPaintCallback callback) {
            std::lock_guard<std::mutex> lock(_acceleratedPaintMutex);
            _acceleratedPaintCallback = std::move(callback);
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

        // Render thread, under LockTexture(): the newest copy, or null. A non-null
        // read is closed with the matching End call once its draws are recorded.
        ID3D11Texture2D *BeginSharedRead() {
            return BeginSharedRead(_sharedPage);
        }
        void EndSharedRead() {
            EndSharedRead(_sharedPage);
        }
        ID3D11Texture2D *BeginPopupSharedRead() {
            return BeginSharedRead(_sharedPopup);
        }
        void EndPopupSharedRead() {
            EndSharedRead(_sharedPopup);
        }

        const std::vector<uint8_t> &GetPixelData() const {
            return _pixelData;
        }

        bool IsPixelDataDirty() const {
            return _pixelDataDirty;
        }

        // Where the pixels changed while dirty; a view uploading only this keeps the
        // rest of its texture from before.
        CefRect GetPixelDirtyRect() const {
            return _pixelDirtyRect;
        }

        void ClearPixelDataDirty() {
            _pixelDataDirty = false;
            _pixelDirtyRect = CefRect();
        }

      private:
        bool CreateCopyDevice();
        bool EnsureSharedCopy(SharedCopy &copy, UINT width, UINT height);
        static void ReleaseSharedCopy(SharedCopy &copy);
        void CopySharedTexture(HANDLE handle, SharedCopy &copy);
        ID3D11Texture2D *BeginSharedRead(SharedCopy &copy);
        void EndSharedRead(SharedCopy &copy);
        void StampPopup();
        void MarkPixelsDirty(const CefRect &rect);
        void UnstampPopup();

        IMPLEMENT_REFCOUNTING(RenderHandler);
    };
} // namespace Framework::GUI::CEF
