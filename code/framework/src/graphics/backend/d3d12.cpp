/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d12.h"

#include <logging/logger.h>

namespace Framework::Graphics {
    bool D3D12Backend::Init(ID3D12Device *device, ID3D12DeviceContext *context, IDXGISwapChain3 *swapChain, ID3D12CommandQueue *commandQueue) {
        _device  = device;
        _context = context;
        // #1 get device from swapchain (maybe different device)
        ID3D12Device *pD3DDevice;
        if (FAILED(swapChain->GetDevice(__uuidof(ID3D12Device), (void **)&pD3DDevice))) {
            return false;
        }

        _swapChain    = swapChain;
        _commandQueue = commandQueue;

        // #2 get count of buffers
        {
            DXGI_SWAP_CHAIN_DESC desc {};
            swapChain->GetDesc(&desc);
            _frameBufferCount = desc.BufferCount;

            _frameContext.clear();
            _frameContext.resize(desc.BufferCount);
        }

        // #3 create srv heap
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc {};
            desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = _frameBufferCount;
            desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            if (pD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_srvHeap)) != S_OK) {
                return false;
            }
        }

        // #3 create rtv heap
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc {};
            desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = _frameBufferCount;
            desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            desc.NodeMask       = 1;

            if (pD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_rtvHeap)) != S_OK) {
                return false;
            }

            const auto rtvDescriptorSize          = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();

            for (UINT i = 0; i < _frameBufferCount; i++) {
                _frameContext[i]._mainRenderTargetDescriptor = rtvHandle;
                swapChain->GetBuffer(i, IID_PPV_ARGS(&_frameContext[i]._mainRenderTargetResource));
                pD3DDevice->CreateRenderTargetView(_frameContext[i]._mainRenderTargetResource, nullptr, rtvHandle);
                rtvHandle.ptr += rtvDescriptorSize;
            }
        }

        {
            ID3D12CommandAllocator *allocator {nullptr};
            if (pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)) != S_OK) {
                return false;
            }

            for (size_t i = 0; i < _frameBufferCount; i++) {
                if (pD3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_frameContext[i]._commandAllocator)) != S_OK) {
                    return false;
                }
            }

            if (pD3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _frameContext[0]._commandAllocator, NULL, IID_PPV_ARGS(&_commandList)) != S_OK || _commandList->Close() != S_OK) {
                return false;
            }
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->info("D3D12 device {}", fmt::ptr(device));
        return true;
    }

    bool D3D12Backend::Shutdown() {
        // release objects
        _rtvHeap->Release();
        _srvHeap->Release();
        _commandList->Release();
        for (auto &frameContext : _frameContext) {
            frameContext._commandAllocator->Release();
            frameContext._mainRenderTargetResource->Release();
        }
        return true;
    }

    void D3D12Backend::Begin() {
        auto &currentFrameContext = _frameContext[_swapChain->GetCurrentBackBufferIndex()];
        currentFrameContext._commandAllocator->Reset();

        _barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        _barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        _barrier.Transition.pResource   = currentFrameContext._mainRenderTargetResource;
        _barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        _barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        _barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;

        _commandList->Reset(currentFrameContext._commandAllocator, nullptr);
        _commandList->ResourceBarrier(1, &_barrier);
        _commandList->OMSetRenderTargets(1, &currentFrameContext._mainRenderTargetDescriptor, FALSE, nullptr);
        _commandList->SetDescriptorHeaps(1, &_srvHeap);
    }

    void D3D12Backend::End() {
        _barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        _barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        _commandList->ResourceBarrier(1, &_barrier);
        _commandList->Close();
        _commandQueue->ExecuteCommandLists(1, (ID3D12CommandList **)&_commandList);
    }

    void D3D12Backend::Update() {}

    int D3D12Backend::NumFramesInFlight() const {
        return _frameBufferCount;
    }
} // namespace Framework::Graphics
