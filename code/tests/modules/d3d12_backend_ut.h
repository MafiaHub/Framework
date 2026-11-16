/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <graphics/backend/d3d12.h>

#include <dxgi1_4.h>

namespace D3D12BackendTests {
    using Microsoft::WRL::ComPtr;

    // Stands in for the game's swapchain. After Free() it models freed memory: the game
    // destroyed it and kept ticking, and any call into it is a use-after-free.
    class FakeSwapChain final: public IDXGISwapChain3 {
        ComPtr<ID3D12Device> _device;
        ComPtr<ID3D12Resource> _buffer;
        DXGI_SWAP_CHAIN_DESC1 _desc {};
        bool _freed = false;

        HRESULT Touch() {
            if (_freed) {
                ++callsAfterFree;
            }
            return E_NOTIMPL;
        }

      public:
        int callsAfterFree = 0;

        FakeSwapChain(ID3D12Device *device, DXGI_FORMAT format, UINT width, UINT height): _device(device) {
            _desc.BufferCount      = 2;
            _desc.SampleDesc.Count = 1;
            _desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            Resize(format, width, height);
        }

        // What ResizeBuffers does: same object, new buffers.
        void Resize(DXGI_FORMAT format, UINT width, UINT height) {
            _desc.Format = format;
            _desc.Width  = width;
            _desc.Height = height;

            D3D12_HEAP_PROPERTIES heap {};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC texture {};
            texture.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            texture.Width            = width;
            texture.Height           = height;
            texture.DepthOrArraySize = 1;
            texture.MipLevels        = 1;
            texture.Format           = format;
            texture.SampleDesc.Count = 1;
            texture.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            _buffer.Reset();
            _device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_PRESENT, nullptr, IID_PPV_ARGS(&_buffer));
        }

        void Free() {
            _freed = true;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **object) override {
            *object = nullptr;
            Touch();
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {
            Touch();
            return 1;
        }
        ULONG STDMETHODCALLTYPE Release() override {
            Touch();
            return 1;
        }

        HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **device) override {
            Touch();
            return _device.CopyTo(riid, device);
        }
        HRESULT STDMETHODCALLTYPE GetBuffer(UINT, REFIID riid, void **surface) override {
            Touch();
            return _buffer ? _buffer.CopyTo(riid, surface) : E_FAIL;
        }
        HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC *desc) override {
            Touch();
            *desc                   = {};
            desc->BufferCount       = _desc.BufferCount;
            desc->BufferDesc.Width  = _desc.Width;
            desc->BufferDesc.Height = _desc.Height;
            desc->BufferDesc.Format = _desc.Format;
            desc->SampleDesc        = _desc.SampleDesc;
            desc->SwapEffect        = _desc.SwapEffect;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1 *desc) override {
            Touch();
            *desc = _desc;
            return S_OK;
        }
        // Not 0, the backend's default, so a cached index is told apart from none.
        UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override {
            Touch();
            return 1;
        }

        // Not used by the backend.
        HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT *, void *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetParent(REFIID, void **) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE Present(UINT, UINT) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL, IDXGIOutput *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL *, IDXGIOutput **) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT, UINT, UINT, DXGI_FORMAT, UINT) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput **) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetHwnd(HWND *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID, void **) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE Present1(UINT, UINT, const DXGI_PRESENT_PARAMETERS *) override {
            return Touch();
        }
        BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override {
            Touch();
            return FALSE;
        }
        HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput **) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE SetSourceSize(UINT, UINT) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetSourceSize(UINT *, UINT *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *) override {
            return Touch();
        }
        HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override {
            Touch();
            return nullptr;
        }
        HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE, UINT *) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE) override {
            return Touch();
        }
        HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT *, IUnknown *const *) override {
            return Touch();
        }
    };

    struct Device {
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;

        bool Create() {
            ComPtr<IDXGIFactory4> factory;
            ComPtr<IDXGIAdapter> adapter;
            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))) {
                return false;
            }
            if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
                return false;
            }
            D3D12_COMMAND_QUEUE_DESC desc {};
            return SUCCEEDED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
        }
    };

    inline bool HasBackBuffer(const Framework::Graphics::D3D12Backend &backend, DXGI_FORMAT format, UINT width, UINT height) {
        UINT actualWidth  = 0;
        UINT actualHeight = 0;
        return backend.GetBackBufferFormat() == format && backend.GetBackBufferSize(actualWidth, actualHeight) && actualWidth == width && actualHeight == height;
    }
} // namespace D3D12BackendTests

MODULE(d3d12_backend, {
    using namespace D3D12BackendTests;

    IT("answers back buffer queries without touching a swapchain the game already freed", {
        Device warp;
        EQUALS(warp.Create(), true);

        FakeSwapChain chain(warp.device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, 1280, 720);
        Framework::Graphics::RendererConfiguration opts {};
        opts.d3d12.swapchain    = &chain;
        opts.d3d12.commandQueue = warp.queue.Get();

        Framework::Graphics::D3D12Backend backend;
        EQUALS(backend.Init(opts), true);
        EQUALS(HasBackBuffer(backend, DXGI_FORMAT_B8G8R8A8_UNORM, 1280, 720), true);

        // ResizeBuffers keeps the pointer; the next frame picks the change up.
        chain.Resize(DXGI_FORMAT_R10G10B10A2_UNORM, 1920, 1080);
        backend.Begin();
        const bool acquired = backend.GetCurrentBackBuffer() != nullptr;
        backend.End();
        // Drained before any check, so a failing one never frees what the queue still reads.
        const bool drained = backend.WaitForGpu();
        EQUALS(acquired, true);
        EQUALS(drained, true);
        EQUALS(HasBackBuffer(backend, DXGI_FORMAT_R10G10B10A2_UNORM, 1920, 1080), true);

        // The game frees its swapchain (exit, mode change) and keeps ticking before any Present.
        chain.Free();
        EQUALS(HasBackBuffer(backend, DXGI_FORMAT_R10G10B10A2_UNORM, 1920, 1080), true);
        EQUALS(backend.GetCurrentFrameIndex(), 1u);
        EQUALS(chain.callsAfterFree, 0);

        // The next Present hands over the replacement.
        FakeSwapChain replacement(warp.device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, 2560, 1440);
        backend.SetSwapChain(&replacement);
        EQUALS(HasBackBuffer(backend, DXGI_FORMAT_R16G16B16A16_FLOAT, 2560, 1440), true);

        backend.Shutdown();
        EQUALS(chain.callsAfterFree, 0);
    });
});
