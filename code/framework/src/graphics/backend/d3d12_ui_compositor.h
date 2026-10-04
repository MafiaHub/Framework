/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/safe_win32.h"

#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl/client.h>

namespace Framework::Graphics {
    // ImGui and browser content use display-encoded sRGB. Compose them there first,
    // then convert the completed, premultiplied overlay for a linear scRGB target.
    class D3D12UICompositor {
        Microsoft::WRL::ComPtr<ID3D12Resource> _surface;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _rtvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> _srvHeap;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> _pipeline;
        UINT _width  = 0;
        UINT _height = 0;

      public:
        static DXGI_FORMAT RenderFormat(DXGI_FORMAT backBufferFormat) {
            return backBufferFormat == DXGI_FORMAT_R16G16B16A16_FLOAT ? DXGI_FORMAT_R8G8B8A8_UNORM : backBufferFormat;
        }

        bool Matches(UINT width, UINT height) const {
            return _surface && _width == width && _height == height;
        }

        // The caller must drain the GPU before resizing or releasing these resources.
        HRESULT Resize(ID3D12Device *device, UINT width, UINT height);
        void Reset();
        void Begin(ID3D12GraphicsCommandList *commands);
        void Composite(ID3D12GraphicsCommandList *commands, D3D12_CPU_DESCRIPTOR_HANDLE target);
    };
} // namespace Framework::Graphics
