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
#include <map>
#include <wrl/client.h>

namespace Framework::Graphics {
    // Draw CEF's premultiplied pixels directly. Owned by the renderer, not a view.
    class D3D12Image {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
        std::map<DXGI_FORMAT, Microsoft::WRL::ComPtr<ID3D12PipelineState>> _pipelines;

        HRESULT Prepare(ID3D12Device *device, DXGI_FORMAT format);

      public:
        // rect is left/top/width/height in clip space. The caller supplies the
        // viewport, scissor, texture heap and RTV, and restores its pipeline after.
        HRESULT Draw(ID3D12Device *device, ID3D12GraphicsCommandList *commands, DXGI_FORMAT format, D3D12_GPU_DESCRIPTOR_HANDLE texture, const float rect[4]);
    };
} // namespace Framework::Graphics
