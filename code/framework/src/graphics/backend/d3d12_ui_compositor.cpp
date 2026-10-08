/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d12_ui_compositor.h"

#include <cstring>
#include <d3dcompiler.h>
#include <utility>

namespace Framework::Graphics {
    namespace {
        constexpr char kShader[] = R"(
            Texture2D overlay : register(t0);
            cbuffer Settings : register(b0) {
                float brightnessScale;
            };

            float4 VS(uint vertex : SV_VertexID) : SV_Position {
                float2 uv = float2((vertex << 1) & 2, vertex & 2);
                return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
            }

            float3 ToLinear(float3 color) {
                return float3(
                    color.r <= 0.04045 ? color.r / 12.92 : pow((color.r + 0.055) / 1.055, 2.4),
                    color.g <= 0.04045 ? color.g / 12.92 : pow((color.g + 0.055) / 1.055, 2.4),
                    color.b <= 0.04045 ? color.b / 12.92 : pow((color.b + 0.055) / 1.055, 2.4));
            }

            float4 PS(float4 position : SV_Position) : SV_Target {
                float4 color = overlay.Load(int3(int2(position.xy), 0));
                // Alpha is coverage, not a color channel. Decode the straight color
                // and then premultiply again for ONE / INV_SRC_ALPHA blending.
                if (color.a == 0)
                    return 0;
                return float4(ToLinear(saturate(color.rgb / color.a)) * color.a * brightnessScale, color.a);
            }
        )";

        void Transition(ID3D12GraphicsCommandList *commands, ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER barrier {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter  = after;
            commands->ResourceBarrier(1, &barrier);
        }
    } // namespace

    HRESULT D3D12UICompositor::Resize(ID3D12Device *device, UINT width, UINT height) {
        if (Matches(width, height)) {
            return S_OK;
        }
        if (width == 0 || height == 0) {
            return E_INVALIDARG;
        }

        // Build a replacement transactionally so a failed allocation can be retried.
        D3D12UICompositor replacement;
        replacement._rootSignature = _rootSignature;
        replacement._pipeline      = _pipeline;
        HRESULT result;
        if (!replacement._pipeline) {
            D3D12_DESCRIPTOR_RANGE range {};
            range.RangeType      = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            D3D12_ROOT_PARAMETER parameters[2] {};
            parameters[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[0].DescriptorTable.NumDescriptorRanges = 1;
            parameters[0].DescriptorTable.pDescriptorRanges   = &range;
            parameters[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[1].Constants.Num32BitValues            = 1;
            D3D12_ROOT_SIGNATURE_DESC rootDesc {};
            rootDesc.NumParameters = 2;
            rootDesc.pParameters   = parameters;

            Microsoft::WRL::ComPtr<ID3DBlob> root, vs, ps;
            if (FAILED(result = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &root, nullptr)) || FAILED(result = device->CreateRootSignature(0, root->GetBufferPointer(), root->GetBufferSize(), IID_PPV_ARGS(&replacement._rootSignature)))
                || FAILED(result = D3DCompile(kShader, std::strlen(kShader), "UI compositor", nullptr, nullptr, "VS", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, nullptr))
                || FAILED(result = D3DCompile(kShader, std::strlen(kShader), "UI compositor", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, nullptr))) {
                return result;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline {};
            pipeline.pRootSignature                                 = replacement._rootSignature.Get();
            pipeline.VS                                             = {vs->GetBufferPointer(), vs->GetBufferSize()};
            pipeline.PS                                             = {ps->GetBufferPointer(), ps->GetBufferSize()};
            pipeline.RasterizerState.FillMode                       = D3D12_FILL_MODE_SOLID;
            pipeline.RasterizerState.CullMode                       = D3D12_CULL_MODE_NONE;
            pipeline.RasterizerState.DepthClipEnable                = TRUE;
            pipeline.DepthStencilState.DepthFunc                    = D3D12_COMPARISON_FUNC_ALWAYS;
            pipeline.DepthStencilState.FrontFace.StencilFailOp      = D3D12_STENCIL_OP_KEEP;
            pipeline.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            pipeline.DepthStencilState.FrontFace.StencilPassOp      = D3D12_STENCIL_OP_KEEP;
            pipeline.DepthStencilState.FrontFace.StencilFunc        = D3D12_COMPARISON_FUNC_ALWAYS;
            pipeline.DepthStencilState.BackFace                     = pipeline.DepthStencilState.FrontFace;
            pipeline.SampleMask                                     = UINT_MAX;
            pipeline.PrimitiveTopologyType                          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pipeline.NumRenderTargets                               = 1;
            pipeline.RTVFormats[0]                                  = DXGI_FORMAT_R16G16B16A16_FLOAT;
            pipeline.SampleDesc.Count                               = 1;
            auto &blend                                             = pipeline.BlendState.RenderTarget[0];
            blend.BlendEnable                                       = TRUE;
            blend.SrcBlend                                          = D3D12_BLEND_ONE;
            blend.DestBlend                                         = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOp                                           = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha                                     = D3D12_BLEND_ONE;
            blend.DestBlendAlpha                                    = D3D12_BLEND_INV_SRC_ALPHA;
            blend.BlendOpAlpha                                      = D3D12_BLEND_OP_ADD;
            blend.RenderTargetWriteMask                             = D3D12_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(result = device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&replacement._pipeline)))) {
                return result;
            }
        }

        D3D12_DESCRIPTOR_HEAP_DESC heap {};
        heap.NumDescriptors = 1;
        heap.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(result = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&replacement._rtvHeap)))) {
            return result;
        }
        heap.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(result = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&replacement._srvHeap)))) {
            return result;
        }

        D3D12_HEAP_PROPERTIES properties {};
        properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC texture {};
        texture.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texture.Width            = width;
        texture.Height           = height;
        texture.DepthOrArraySize = 1;
        texture.MipLevels        = 1;
        texture.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear {};
        clear.Format = texture.Format;
        if (FAILED(result = device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&replacement._surface)))) {
            return result;
        }
        device->CreateRenderTargetView(replacement._surface.Get(), nullptr, replacement._rtvHeap->GetCPUDescriptorHandleForHeapStart());
        device->CreateShaderResourceView(replacement._surface.Get(), nullptr, replacement._srvHeap->GetCPUDescriptorHandleForHeapStart());
        replacement._width  = width;
        replacement._height = height;
        *this               = std::move(replacement);
        return S_OK;
    }

    void D3D12UICompositor::Reset() {
        *this = D3D12UICompositor {};
    }

    void D3D12UICompositor::Begin(ID3D12GraphicsCommandList *commands) {
        Transition(commands, _surface.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const auto rtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        const float clear[4] {};
        commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
        commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }

    void D3D12UICompositor::Composite(ID3D12GraphicsCommandList *commands, D3D12_CPU_DESCRIPTOR_HANDLE target, float brightnessScale) {
        Transition(commands, _surface.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
        auto *heap = _srvHeap.Get();
        commands->SetDescriptorHeaps(1, &heap);
        commands->SetGraphicsRootSignature(_rootSignature.Get());
        commands->SetPipelineState(_pipeline.Get());
        commands->SetGraphicsRootDescriptorTable(0, _srvHeap->GetGPUDescriptorHandleForHeapStart());
        commands->SetGraphicsRoot32BitConstants(1, 1, &brightnessScale, 0);
        const D3D12_VIEWPORT viewport {0, 0, static_cast<float>(_width), static_cast<float>(_height), 0, 1};
        const D3D12_RECT scissor {0, 0, static_cast<LONG>(_width), static_cast<LONG>(_height)};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->DrawInstanced(3, 1, 0, 0);
    }
} // namespace Framework::Graphics
