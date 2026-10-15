/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "render_handler.h"

#include <logging/logger.h>
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
        {
            std::lock_guard<std::mutex> lock(_acceleratedPaintMutex);
            if (_acceleratedPaintCallback) {
                if (info.shared_texture_handle) {
                    _acceleratedPaintCallback(type, info.shared_texture_handle);
                }
                return;
            }
        }

        if (!_device || !info.shared_texture_handle || _copyFailed) {
            return;
        }

        FW_PROFILE_SCOPE_N("Cef::OnAcceleratedPaint");
        if (!_copyDevice && !CreateCopyDevice()) {
            // Once is enough: every later frame would fail the same way
            _copyFailed = true;
            Framework::Logging::GetLogger("Web")->error("RenderHandler: could not create the shared texture copy device");
            return;
        }

        CopySharedTexture(info.shared_texture_handle, type == PET_POPUP ? _sharedPopup : _sharedPage);
    }

    bool RenderHandler::CreateCopyDevice() {
        // Same adapter as the game, or the textures never open on its device
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter> gameAdapter;
        DXGI_ADAPTER_DESC gameAdapterDesc {};
        if (FAILED(_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || FAILED(dxgiDevice->GetAdapter(&gameAdapter)) || FAILED(gameAdapter->GetDesc(&gameAdapterDesc))) {
            return false;
        }

        // The game's adapter may come from a DXGI 1.0 factory, and a device made on
        // that cannot create NT-handle shared textures (E_INVALIDARG). Find the same
        // adapter again through a DXGI 1.1 factory.
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return false;
        }
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 desc {};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.AdapterLuid.LowPart == gameAdapterDesc.AdapterLuid.LowPart && desc.AdapterLuid.HighPart == gameAdapterDesc.AdapterLuid.HighPart) {
                break;
            }
            adapter.Reset();
        }
        if (!adapter) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &_copyContext))) {
            return false;
        }
        if (FAILED(device.As(&_copyDevice))) {
            return false;
        }

        D3D11_QUERY_DESC queryDesc {};
        queryDesc.Query = D3D11_QUERY_EVENT;
        return SUCCEEDED(_copyDevice->CreateQuery(&queryDesc, &_copyDone));
    }

    bool RenderHandler::EnsureSharedCopy(SharedCopy &copy, UINT width, UINT height) {
        if (copy.width == width && copy.height == height) {
            return true;
        }
        // The runtime keeps a texture alive for GPU work already queued on it
        ReleaseSharedCopy(copy);

        D3D11_TEXTURE2D_DESC desc {};
        desc.Width            = width;
        desc.Height           = height;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DEFAULT;
        // Sharing is refused (E_INVALIDARG) without the render-target bind
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        // A plain legacy handle: a game device made on a DXGI 1.0 adapter opens
        // neither NT handles nor keyed-mutex textures (E_INVALIDARG)
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

        D3D11_QUERY_DESC queryDesc {};
        queryDesc.Query = D3D11_QUERY_EVENT;

        const auto fail = [&copy](const char *step, HRESULT result) {
            Framework::Logging::GetLogger("Web")->error("RenderHandler: shared copy {} failed: {:#x}", step, static_cast<unsigned>(result));
            ReleaseSharedCopy(copy);
            return false;
        };

        for (auto &buffer : copy.buffers) {
            Microsoft::WRL::ComPtr<IDXGIResource> resource;
            HANDLE handle = nullptr;
            if (const auto hr = _copyDevice->CreateTexture2D(&desc, nullptr, &buffer.owned); FAILED(hr)) {
                return fail("CreateTexture2D", hr);
            }
            if (const auto hr = buffer.owned.As(&resource); FAILED(hr)) {
                return fail("IDXGIResource", hr);
            }
            if (const auto hr = resource->GetSharedHandle(&handle); FAILED(hr)) {
                return fail("GetSharedHandle", hr);
            }
            // The game device refusing these will refuse them every time: stop
            // retrying on each paint
            if (const auto hr = _device->OpenSharedResource(handle, IID_PPV_ARGS(&buffer.game)); FAILED(hr)) {
                _copyFailed = true;
                return fail("OpenSharedResource", hr);
            }
            if (const auto hr = _device->CreateQuery(&queryDesc, &buffer.read); FAILED(hr)) {
                _copyFailed = true;
                return fail("CreateQuery", hr);
            }
        }

        copy.width  = width;
        copy.height = height;
        return true;
    }

    void RenderHandler::ReleaseSharedCopy(SharedCopy &copy) {
        copy.buffers = {};
        copy.latest  = -1;
        copy.width   = 0;
        copy.height  = 0;
    }

    void RenderHandler::CopySharedTexture(HANDLE handle, SharedCopy &copy) {
        constexpr DWORD kCopyTimeoutMs = 1000;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
        if (FAILED(_copyDevice->OpenSharedResource1(handle, IID_PPV_ARGS(&source)))) {
            // A texture from another adapter never opens; see force-high-performance-gpu
            Framework::Logging::GetLogger("Web")->error("RenderHandler: could not open CEF's shared texture");
            return;
        }
        D3D11_TEXTURE2D_DESC sourceDesc {};
        source->GetDesc(&sourceDesc);

        // Only this thread replaces the buffers, so the texture outlives the lock
        int slot = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> target;
        {
            std::lock_guard<std::mutex> lock(_textureMutex);
            if (!EnsureSharedCopy(copy, sourceDesc.Width, sourceDesc.Height)) {
                return;
            }

            // A buffer the game is not drawing from. Should every one still be in
            // flight, the oldest is overwritten: a torn frame, never a lost one.
            const int count = static_cast<int>(copy.buffers.size());
            slot            = (copy.latest + 1) % count;
            for (int i = 1; i < count; i++) {
                const int candidate = (copy.latest + i) % count;
                if (!copy.buffers[candidate].reading) {
                    slot = candidate;
                    break;
                }
            }
            target = copy.buffers[slot].owned;
        }

        _copyContext->CopyResource(target.Get(), source.Get());

        // CEF hands the source back to its pool as soon as we return, and the game
        // device only sees the copy once it has landed
        {
            FW_PROFILE_SCOPE_N("Cef::OnAcceleratedPaint::Wait");
            _copyContext->End(_copyDone.Get());
            _copyContext->Flush();
            const auto deadline = GetTickCount64() + kCopyTimeoutMs;
            while (_copyContext->GetData(_copyDone.Get(), nullptr, 0, 0) == S_FALSE) {
                if (GetTickCount64() > deadline) {
                    Framework::Logging::GetLogger("Web")->error("RenderHandler: shared texture copy timed out");
                    return;
                }
                YieldProcessor();
            }
        }

        std::lock_guard<std::mutex> lock(_textureMutex);
        copy.latest = slot;

        if (!_copyLogged) {
            _copyLogged = true;
            Framework::Logging::GetLogger("Web")->debug("RenderHandler: accelerated paint active ({}x{})", copy.width, copy.height);
        }
    }

    ID3D11Texture2D *RenderHandler::BeginSharedRead(SharedCopy &copy) {
        if (copy.latest < 0) {
            return nullptr;
        }

        // Hand back every buffer whose last draw has run
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        _device->GetImmediateContext(&context);
        for (auto &buffer : copy.buffers) {
            if (buffer.reading && context->GetData(buffer.read.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
                buffer.reading = false;
            }
        }
        return copy.buffers[copy.latest].game.Get();
    }

    void RenderHandler::EndSharedRead(SharedCopy &copy) {
        auto &buffer = copy.buffers[copy.latest];
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        _device->GetImmediateContext(&context);
        context->End(buffer.read.Get());
        buffer.reading = true;
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
                MarkPixelsDirty(CefRect(0, 0, width, height));
            }
            else {
                copied = CopyDirtyRects(dirtyRects, _pixelData.data(), src, width, height);
                for (const auto &rect : dirtyRects) {
                    MarkPixelsDirty(rect);
                }
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
        ReleaseSharedCopy(_sharedPopup);
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
        MarkPixelsDirty(_stampedRect);
        _popupUnderlay.resize(static_cast<size_t>(_stampedRect.width) * 4 * static_cast<size_t>(_stampedRect.height));
        CopyBlock(_popupUnderlay.data(), _stampedRect.width, 0, 0, _pixelData.data(), _pixelWidth, x0, y0, _stampedRect.width, _stampedRect.height);
        CopyBlock(_pixelData.data(), _pixelWidth, x0, y0, _popupPixels.data(), _popupPixelWidth, x0 - _popupRect.x, y0 - _popupRect.y, _stampedRect.width, _stampedRect.height);
    }

    void RenderHandler::UnstampPopup() {
        if (_stampedRect.IsEmpty()) {
            return;
        }

        CopyBlock(_pixelData.data(), _pixelWidth, _stampedRect.x, _stampedRect.y, _popupUnderlay.data(), _stampedRect.width, 0, 0, _stampedRect.width, _stampedRect.height);
        MarkPixelsDirty(_stampedRect);
        _stampedRect = CefRect();
    }

    void RenderHandler::MarkPixelsDirty(const CefRect &rect) {
        const int x0 = (std::max)(0, rect.x);
        const int y0 = (std::max)(0, rect.y);
        const int x1 = (std::min)(_pixelWidth, rect.x + rect.width);
        const int y1 = (std::min)(_pixelHeight, rect.y + rect.height);
        if (x1 <= x0 || y1 <= y0) {
            return;
        }

        if (_pixelDirtyRect.IsEmpty()) {
            _pixelDirtyRect = CefRect(x0, y0, x1 - x0, y1 - y0);
            return;
        }
        const int left   = (std::min)(_pixelDirtyRect.x, x0);
        const int top    = (std::min)(_pixelDirtyRect.y, y0);
        const int right  = (std::max)(_pixelDirtyRect.x + _pixelDirtyRect.width, x1);
        const int bottom = (std::max)(_pixelDirtyRect.y + _pixelDirtyRect.height, y1);
        _pixelDirtyRect  = CefRect(left, top, right - left, bottom - top);
    }
} // namespace Framework::GUI::CEF
