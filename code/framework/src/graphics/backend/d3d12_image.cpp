/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d12_image.h"

#include <cstring>
#include <d3dcompiler.h>

namespace Framework::Graphics {
    namespace {
        constexpr char kShader[] = R"(
            cbuffer Geometry : register(b0) { float4 rect; };
            Texture2D image : register(t0);
            SamplerState imageSampler : register(s0);
            struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };

            Vertex VS(uint id : SV_VertexID) {
                const uint corners[6] = {0, 1, 2, 1, 3, 2};
                Vertex vertex;
                vertex.uv = float2(corners[id] & 1, corners[id] >> 1);
                vertex.position = float4(rect.xy + vertex.uv * rect.zw, 0, 1);
                return vertex;
            }

            float4 PS(Vertex vertex) : SV_Target {
                return image.Sample(imageSampler, vertex.uv);
            }
        )";
    } // namespace

    HRESULT D3D12Image::Prepare(ID3D12Device *device, DXGI_FORMAT format) {
        if (_pipelines.contains(format)) {
            return S_OK;
        }
        HRESULT result;
        if (!_rootSignature) {
            D3D12_DESCRIPTOR_RANGE range {};
            range.RangeType      = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            D3D12_ROOT_PARAMETER parameters[2] {};
            parameters[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
            parameters[0].DescriptorTable          = {1, &range};
            parameters[1].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[1].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
            parameters[1].Constants.Num32BitValues = 4;
            D3D12_STATIC_SAMPLER_DESC sampler {};
            sampler.Filter   = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc                                 = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.MaxLOD                                         = D3D12_FLOAT32_MAX;
            sampler.ShaderVisibility                               = D3D12_SHADER_VISIBILITY_PIXEL;
            D3D12_ROOT_SIGNATURE_DESC root {};
            root.NumParameters     = 2;
            root.pParameters       = parameters;
            root.NumStaticSamplers = 1;
            root.pStaticSamplers   = &sampler;
            Microsoft::WRL::ComPtr<ID3DBlob> blob;
            if (FAILED(result = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr)) || FAILED(result = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&_rootSignature)))) {
                return result;
            }
        }

        Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
        if (FAILED(result = D3DCompile(kShader, std::strlen(kShader), "Premultiplied image", nullptr, nullptr, "VS", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vs, nullptr))
            || FAILED(result = D3DCompile(kShader, std::strlen(kShader), "Premultiplied image", nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &ps, nullptr))) {
            return result;
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline {};
        pipeline.pRootSignature                                 = _rootSignature.Get();
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
        pipeline.RTVFormats[0]                                  = format;
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
        Microsoft::WRL::ComPtr<ID3D12PipelineState> state;
        if (FAILED(result = device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&state)))) {
            return result;
        }
        // Keep each format alive until renderer teardown, including mode switches
        // while an earlier frame is still executing. No per-repaint GPU drain.
        _pipelines.emplace(format, state);
        return S_OK;
    }

    HRESULT D3D12Image::Draw(ID3D12Device *device, ID3D12GraphicsCommandList *commands, DXGI_FORMAT format, D3D12_GPU_DESCRIPTOR_HANDLE texture, const float rect[4]) {
        const HRESULT result = Prepare(device, format);
        if (FAILED(result)) {
            return result;
        }
        commands->SetGraphicsRootSignature(_rootSignature.Get());
        commands->SetPipelineState(_pipelines.at(format).Get());
        commands->SetGraphicsRootDescriptorTable(0, texture);
        commands->SetGraphicsRoot32BitConstants(1, 4, rect, 0);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->DrawInstanced(6, 1, 0, 0);
        return S_OK;
    }
} // namespace Framework::Graphics
