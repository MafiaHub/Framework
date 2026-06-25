/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d12.h"

#include <logging/logger.h>
#include <utils/process_shutdown.h>

#include <wrl/client.h>

namespace Framework::Graphics {
    bool D3D12Backend::Init(const Framework::Graphics::RendererConfiguration &opts) {
        const auto swapChain = opts.d3d12.swapchain;
        const auto commandQueue = opts.d3d12.commandQueue;
        _context = opts.d3d12.deviceContext;
        // #1 get device from swapchain (maybe different device)
        Microsoft::WRL::ComPtr<ID3D12Device> deviceGuard;
        if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&deviceGuard)))) {
            return false;
        }
        ID3D12Device *pD3DDevice = deviceGuard.Get();

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
            desc.NumDescriptors = _frameBufferCount + kExtraSrvSlots;
            desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            if (pD3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&_srvHeap)) != S_OK) {
                return false;
            }

            // Manage slots that are not reserved for ImGui and headroom
            _srvDescriptorSize = pD3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            _srvHeapSize       = desc.NumDescriptors;
            _srvSlotInUse.assign(_srvHeapSize, false);
            _freeSrvSlots.clear();
            const auto extraSrvStart = _frameBufferCount - 1;
            for (UINT i = kExtraSrvSlots; i > 0; i--) {
                _freeSrvSlots.push_back(extraSrvStart + i);
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

        _device = deviceGuard.Detach();

        Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->info("D3D12 device {}", fmt::ptr(_device));
        return true;
    }

    void D3D12Backend::Shutdown() {
        // release objects
        _rtvHeap->Release();
        _srvHeap->Release();
        _srvHeap = nullptr;
        _commandList->Release();
        for (const auto &frameContext : _frameContext) {
            frameContext._commandAllocator->Release();
            frameContext._mainRenderTargetResource->Release();
        }
        if (_device) {
            _device->Release();
            _device = nullptr;
        }
    }

    void D3D12Backend::Begin() {
        const auto &currentFrameContext = _frameContext[_swapChain->GetCurrentBackBufferIndex()];
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

    int D3D12Backend::AllocateSRVSlot() {
        std::lock_guard<std::mutex> lock(_srvMutex);
        if (!_srvHeap) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D12Backend::AllocateSRVSlot, no descriptor heap");
            return -1;
        }
        if (_freeSrvSlots.empty()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D12Backend::AllocateSRVSlot, heap exhausted");
            return -1;
        }
        const auto slot     = _freeSrvSlots.back();
        _freeSrvSlots.pop_back();
        _srvSlotInUse[slot] = true;
        return static_cast<int>(slot);
    }

    void D3D12Backend::FreeSRVSlot(int slot) {
        std::lock_guard<std::mutex> lock(_srvMutex);
        // reject out-of-pool indices and double-frees so a bad id can never
        // re-enter the free list and alias a live slot
        if (slot < static_cast<int>(_frameBufferCount) || slot >= static_cast<int>(_srvHeapSize)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D12Backend::FreeSRVSlot, slot {} out of pool range", slot);
            return;
        }
        if (!_srvSlotInUse[slot]) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D12Backend::FreeSRVSlot, slot {} not allocated", slot);
            return;
        }
        _srvSlotInUse[slot] = false;
        _freeSrvSlots.push_back(static_cast<UINT>(slot));
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12Backend::GetSRVSlotCPUHandle(int slot) const {
        if (!_srvHeap || slot < 0 || slot >= static_cast<int>(_srvHeapSize)) {
            return {};
        }
        auto handle = _srvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(slot) * _srvDescriptorSize;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12Backend::GetSRVSlotGPUHandle(int slot) const {
        if (!_srvHeap || slot < 0 || slot >= static_cast<int>(_srvHeapSize)) {
            return {};
        }
        auto handle = _srvHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(slot) * _srvDescriptorSize;
        return handle;
    }

    size_t D3D12Backend::GetFreeSRVSlotCount() const {
        std::lock_guard<std::mutex> lock(_srvMutex);
        return _freeSrvSlots.size();
    }

    bool D3D12Backend::WaitForGpu() {
        if (!_commandQueue || !_device) {
            return true; // nothing to drain
        }

        // teardown: device/queue are gone, so the fence can't signal; freeing is
        // safe since in-flight work died with the queue's threads
        if (Framework::Utils::IsProcessShutdownInProgress()) {
            return true;
        }

        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        if (FAILED(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D12Backend::WaitForGpu, CreateFence failed");
            return false;
        }

        const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D12Backend::WaitForGpu, CreateEvent failed");
            return false;
        }

        bool drained = false;
        if (FAILED(_commandQueue->Signal(fence.Get(), 1))) {
            // likely device removed; can't confirm the drain, so report failure
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D12Backend::WaitForGpu, queue Signal failed (device removed?)");
        }
        else if (fence->GetCompletedValue() >= 1) {
            drained = true;
        }
        else if (SUCCEEDED(fence->SetEventOnCompletion(1, event))) {
            // a live GPU signals in single-digit ms; a timeout means a wedged
            // queue, exactly when freeing its resources is unsafe — so fail
            if (WaitForSingleObject(event, 500) == WAIT_OBJECT_0 || fence->GetCompletedValue() >= 1) {
                drained = true;
            }
            else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D12Backend::WaitForGpu, fence wait timed out");
            }
        }
        else {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D12Backend::WaitForGpu, SetEventOnCompletion failed");
        }
        CloseHandle(event);
        return drained;
    }
} // namespace Framework::Graphics
