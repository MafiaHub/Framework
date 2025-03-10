/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d11.h"

namespace Framework::Graphics {
    bool D3D11Backend::Init(ID3D11Device *device, ID3D11DeviceContext *context, IDXGISwapChain *swapChain, void *) {
        _device  = device;
        _context = context;
        _swapChain = swapChain;

        {
            // Create Enabled Blend State

            D3D11_RENDER_TARGET_BLEND_DESC rt_blend_desc;
            ZeroMemory(&rt_blend_desc, sizeof(rt_blend_desc));
            rt_blend_desc.BlendEnable           = true;
            rt_blend_desc.SrcBlend              = D3D11_BLEND_ONE;
            rt_blend_desc.DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
            rt_blend_desc.BlendOp               = D3D11_BLEND_OP_ADD;
            rt_blend_desc.SrcBlendAlpha         = D3D11_BLEND_INV_DEST_ALPHA;
            rt_blend_desc.DestBlendAlpha        = D3D11_BLEND_ONE;
            rt_blend_desc.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            rt_blend_desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

            D3D11_BLEND_DESC blend_desc;
            ZeroMemory(&blend_desc, sizeof(blend_desc));
            blend_desc.AlphaToCoverageEnable  = false;
            blend_desc.IndependentBlendEnable = false;
            blend_desc.RenderTarget[0]        = rt_blend_desc;

            _device->CreateBlendState(&blend_desc, _blendState.GetAddressOf());
        }

        {
            // Create Disabled Blend State

            D3D11_RENDER_TARGET_BLEND_DESC rt_disabled_blend_desc;
            ZeroMemory(&rt_disabled_blend_desc, sizeof(rt_disabled_blend_desc));
            rt_disabled_blend_desc.BlendEnable           = false;
            rt_disabled_blend_desc.SrcBlend              = D3D11_BLEND_ONE;
            rt_disabled_blend_desc.DestBlend             = D3D11_BLEND_ZERO;
            rt_disabled_blend_desc.BlendOp               = D3D11_BLEND_OP_ADD;
            rt_disabled_blend_desc.SrcBlendAlpha         = D3D11_BLEND_ONE;
            rt_disabled_blend_desc.DestBlendAlpha        = D3D11_BLEND_ZERO;
            rt_disabled_blend_desc.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            rt_disabled_blend_desc.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

            D3D11_BLEND_DESC disabled_blend_desc;
            ZeroMemory(&disabled_blend_desc, sizeof(disabled_blend_desc));
            disabled_blend_desc.AlphaToCoverageEnable  = false;
            disabled_blend_desc.IndependentBlendEnable = false;
            disabled_blend_desc.RenderTarget[0]        = rt_disabled_blend_desc;

            _device->CreateBlendState(&disabled_blend_desc, _disabledBlendState.GetAddressOf());
        }

        {
            D3D11_RASTERIZER_DESC rasterizer_desc;
            ZeroMemory(&rasterizer_desc, sizeof(rasterizer_desc));
            rasterizer_desc.FillMode              = D3D11_FILL_SOLID;
            rasterizer_desc.CullMode              = D3D11_CULL_NONE;
            rasterizer_desc.FrontCounterClockwise = false;
            rasterizer_desc.DepthBias             = 0;
            rasterizer_desc.SlopeScaledDepthBias  = 0.0f;
            rasterizer_desc.DepthBiasClamp        = 0.0f;
            rasterizer_desc.DepthClipEnable       = false;
            rasterizer_desc.ScissorEnable         = false;
#if ENABLE_MSAA
            rasterizer_desc.MultisampleEnable     = true;
            rasterizer_desc.AntialiasedLineEnable = true;
#else
            rasterizer_desc.MultisampleEnable     = false;
            rasterizer_desc.AntialiasedLineEnable = false;
#endif

            _device->CreateRasterizerState(&rasterizer_desc, _rsState.GetAddressOf());
        }

        {
            D3D11_RASTERIZER_DESC scissor_rasterizer_desc;
            ZeroMemory(&scissor_rasterizer_desc, sizeof(scissor_rasterizer_desc));
            scissor_rasterizer_desc.FillMode              = D3D11_FILL_SOLID;
            scissor_rasterizer_desc.CullMode              = D3D11_CULL_NONE;
            scissor_rasterizer_desc.FrontCounterClockwise = false;
            scissor_rasterizer_desc.DepthBias             = 0;
            scissor_rasterizer_desc.SlopeScaledDepthBias  = 0.0f;
            scissor_rasterizer_desc.DepthBiasClamp        = 0.0f;
            scissor_rasterizer_desc.DepthClipEnable       = false;
            scissor_rasterizer_desc.ScissorEnable         = true;
#if ENABLE_MSAA
            scissor_rasterizer_desc.MultisampleEnable     = true;
            scissor_rasterizer_desc.AntialiasedLineEnable = true;
#else
            scissor_rasterizer_desc.MultisampleEnable     = false;
            scissor_rasterizer_desc.AntialiasedLineEnable = false;
#endif

            _device->CreateRasterizerState(&scissor_rasterizer_desc, _scissoredRsState.GetAddressOf());
        }

        _device->CreateDeferredContext(0, _deferredContext.GetAddressOf());

        return true;
    }

    bool D3D11Backend::Shutdown() {
        if (_device) {
            if (_deferredContext) {
                _deferredContext->ClearState();
            }
        }
        return true;
    }

    void D3D11Backend::Update() {}

    ID3D11DeviceContext *D3D11Backend::GetImmediateContext() {
        return _immediateContext.Get();
    }

    ID3D11DeviceContext *D3D11Backend::GetDeferredContext() {
        return _deferredContext.Get();
    }

    ID3D11DeviceContext *D3D11Backend::GetContext() {
        return _deferredContext.Get() ? _deferredContext.Get() : _immediateContext.Get();
    }

    void D3D11Backend::EnableBlend() {
        GetContext()->OMSetBlendState(_blendState.Get(), NULL, 0xffffffff);
    }

    void D3D11Backend::DisableBlend() {
        GetContext()->OMSetBlendState(_disabledBlendState.Get(), NULL, 0xffffffff);
    }

    void D3D11Backend::EnableScissor() {
        GetContext()->RSSetState(_scissoredRsState.Get());
    }

    void D3D11Backend::DisableScissor() {
        GetContext()->RSSetState(_rsState.Get());
    }

    void D3D11Backend::BeginDrawing() {}
    void D3D11Backend::EndDrawing() {}
    void D3D11Backend::BindTexture(uint8_t texture_unit, uint32_t texture_id) {}
    void D3D11Backend::BindRenderBuffer(uint32_t render_buffer_id) {}
    void D3D11Backend::ClearRenderBuffer(uint32_t render_buffer_id) {}
    void D3D11Backend::DrawGeometry(uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const GPUState &state) {}
} // namespace Framework::Graphics
