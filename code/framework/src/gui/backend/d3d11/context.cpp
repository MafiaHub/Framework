#include "context.h"

#include <cassert>

namespace ultralight {
    void GPUContextD3D11::Init() {
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

            device()->CreateBlendState(&blend_desc, blend_state_.GetAddressOf());
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

            device()->CreateBlendState(&disabled_blend_desc, disabled_blend_state_.GetAddressOf());
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

            device()->CreateRasterizerState(&rasterizer_desc, rasterizer_state_.GetAddressOf());
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

            device()->CreateRasterizerState(&scissor_rasterizer_desc, scissored_rasterizer_state_.GetAddressOf());
        }

        device()->CreateDeferredContext(0, deferred_context_.GetAddressOf());
    }

    GPUContextD3D11::GPUContextD3D11(ID3D11Device *device, ID3D11DeviceContext *immediate_context, IDXGISwapChain *swap_chain): device_(device), immediate_context_(immediate_context), swap_chain_(swap_chain) {}

    GPUContextD3D11::~GPUContextD3D11() {
        if (device_) {
            if (deferred_context_)
                deferred_context_->ClearState();
                // immediate_context_->ClearState(); // no, because here we don't own it

#ifdef _DEBUG
            ID3D11Debug *debug;
            HRESULT result = device_.Get()->QueryInterface(__uuidof(ID3D11Debug), reinterpret_cast<void **>(&debug));

            if (SUCCEEDED(result)) {
                debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
                debug->Release();
            }
#endif
        }
    }

    void GPUContextD3D11::BeginDrawing() {}

    void GPUContextD3D11::EndDrawing() {}

    ID3D11Device *GPUContextD3D11::device() {
        return device_.Get();
    }

    ID3D11DeviceContext *GPUContextD3D11::immediate_context() {
        return immediate_context_.Get();
    }
    ID3D11DeviceContext *GPUContextD3D11::deferred_context() {
        return deferred_context_.Get();
    }
    ID3D11DeviceContext *GPUContextD3D11::context() {
        return deferred_context_.Get() != nullptr ? deferred_context() : immediate_context();
    }

    void GPUContextD3D11::EnableBlend() {
        context()->OMSetBlendState(blend_state_.Get(), NULL, 0xffffffff);
    }

    void GPUContextD3D11::DisableBlend() {
        context()->OMSetBlendState(disabled_blend_state_.Get(), NULL, 0xffffffff);
    }

    void GPUContextD3D11::EnableScissor() {
        context()->RSSetState(scissored_rasterizer_state_.Get());
    }

    void GPUContextD3D11::DisableScissor() {
        context()->RSSetState(rasterizer_state_.Get());
    }
    IDXGISwapChain *GPUContextD3D11::swap_chain() {
        return swap_chain_.Get();
    }

} // namespace ultralight
