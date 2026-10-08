/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <graphics/backend/d3d12_ui_compositor.h>

#include <DirectXPackedVector.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#include <cmath>
#include <stdexcept>

namespace D3D12UICompositorTests {
    using Microsoft::WRL::ComPtr;

    inline void Check(HRESULT result, const char *operation) {
        if (FAILED(result)) {
            char message[256];
            snprintf(message, sizeof(message), "%s failed: HRESULT 0x%08lx", operation, static_cast<unsigned long>(result));
            throw std::runtime_error(message);
        }
    }

    // Five pixels suffice to test the real compiled compositor and blend state,
    // without a window, game, hardware GPU, or FrameworkClient/CEF dependency.
    class RenderFixture final {
        ComPtr<ID3D12Device> _device;
        ComPtr<ID3D12CommandQueue> _queue;
        ComPtr<ID3D12CommandAllocator> _allocator;
        ComPtr<ID3D12GraphicsCommandList> _commands;
        ComPtr<ID3D12Fence> _fence;
        ComPtr<ID3D12Resource> _target, _readback;
        ComPtr<ID3D12DescriptorHeap> _rtv;
        ComPtr<ID3D12RootSignature> _root;
        ComPtr<ID3D12PipelineState> _pipeline;
        Framework::Graphics::D3D12UICompositor _compositor;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT _footprint {};
        UINT64 _fenceValue = 0;

      public:
        RenderFixture() {
            ComPtr<IDXGIFactory4> factory;
            ComPtr<IDXGIAdapter> adapter;
            Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
            Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter (WARP required)");
            Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device)), "D3D12CreateDevice (WARP required)");
            D3D12_COMMAND_QUEUE_DESC queue {};
            Check(_device->CreateCommandQueue(&queue, IID_PPV_ARGS(&_queue)), "CreateCommandQueue");
            Check(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_allocator)), "CreateCommandAllocator");
            Check(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _allocator.Get(), nullptr, IID_PPV_ARGS(&_commands)), "CreateCommandList");
            Check(_commands->Close(), "Close initial command list");
            Check(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)), "CreateFence");

            D3D12_HEAP_PROPERTIES heap {};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC texture {};
            texture.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            texture.Width            = 5;
            texture.Height           = 1;
            texture.DepthOrArraySize = 1;
            texture.MipLevels        = 1;
            texture.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
            texture.SampleDesc.Count = 1;
            texture.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            Check(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&_target)), "Create FP16 target");
            D3D12_DESCRIPTOR_HEAP_DESC rtv {};
            rtv.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtv.NumDescriptors = 1;
            Check(_device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&_rtv)), "Create RTV heap");
            _device->CreateRenderTargetView(_target.Get(), nullptr, _rtv->GetCPUDescriptorHandleForHeapStart());
            UINT64 bytes;
            _device->GetCopyableFootprints(&texture, 0, 1, 0, &_footprint, nullptr, nullptr, &bytes);
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC readback {};
            readback.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            readback.Width            = bytes;
            readback.Height           = 1;
            readback.DepthOrArraySize = 1;
            readback.MipLevels        = 1;
            readback.SampleDesc.Count = 1;
            readback.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            Check(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &readback, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_readback)), "Create readback buffer");
            Check(_compositor.Resize(_device.Get(), 5, 1), "Resize compositor");

            // Write known premultiplied sRGB values through Begin's public bound
            // target. The production shader itself is compiled by Resize above.
            constexpr char shader[] = R"(
                cbuffer Pixel : register(b0) { float4 color; };
                float4 VS(uint vertex : SV_VertexID) : SV_Position {
                    float2 uv = float2((vertex << 1) & 2, vertex & 2);
                    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
                }
                float4 PS() : SV_Target { return color; }
            )";
            D3D12_ROOT_PARAMETER parameter {};
            parameter.ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameter.ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;
            parameter.Constants.Num32BitValues = 4;
            D3D12_ROOT_SIGNATURE_DESC root {};
            root.NumParameters = 1;
            root.pParameters   = &parameter;
            ComPtr<ID3DBlob> serialized, vs, ps;
            Check(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, nullptr), "Serialize test root signature");
            Check(_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&_root)), "Create test root signature");
            Check(D3DCompile(shader, sizeof(shader) - 1, "UI compositor test", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vs, nullptr), "Compile test VS");
            Check(D3DCompile(shader, sizeof(shader) - 1, "UI compositor test", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &ps, nullptr), "Compile test PS");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline {};
            pipeline.pRootSignature                                 = _root.Get();
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
            pipeline.RTVFormats[0]                                  = DXGI_FORMAT_R8G8B8A8_UNORM;
            pipeline.SampleDesc.Count                               = 1;
            auto &blend                                             = pipeline.BlendState.RenderTarget[0];
            blend.SrcBlend                                          = D3D12_BLEND_ONE;
            blend.DestBlend                                         = D3D12_BLEND_ZERO;
            blend.BlendOp                                           = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha                                     = D3D12_BLEND_ONE;
            blend.DestBlendAlpha                                    = D3D12_BLEND_ZERO;
            blend.BlendOpAlpha                                      = D3D12_BLEND_OP_ADD;
            blend.LogicOp                                           = D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask                             = D3D12_COLOR_WRITE_ENABLE_ALL;
            Check(_device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&_pipeline)), "Create test pipeline");
        }

        void Render(float scale, float (&pixels)[5][4]) {
            Check(_allocator->Reset(), "Reset allocator");
            Check(_commands->Reset(_allocator.Get(), nullptr), "Reset commands");
            const float scene[] = {0.25f, 0.5f, 2.0f, 0.25f};
            const auto target   = _rtv->GetCPUDescriptorHandleForHeapStart();
            _commands->ClearRenderTargetView(target, scene, 0, nullptr);
            _compositor.Begin(_commands.Get());
            _commands->SetGraphicsRootSignature(_root.Get());
            _commands->SetPipelineState(_pipeline.Get());
            const D3D12_VIEWPORT viewport {0, 0, 5, 1, 0, 1};
            _commands->RSSetViewports(1, &viewport);
            _commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            const float colors[][4] = {
                {1, 1, 1, 1}, {128.0f / 255, 64.0f / 255, 10.0f / 255, 1}, {128.0f / 255, 64.0f / 255, 32.0f / 255, 128.0f / 255}, {1, 0.5f, 0.25f, 0}, // zero coverage must ignore even nonzero RGB
            };
            for (LONG pixel = 0; pixel < 4; ++pixel) {
                const D3D12_RECT scissor {pixel, 0, pixel + 1, 1};
                _commands->RSSetScissorRects(1, &scissor);
                _commands->SetGraphicsRoot32BitConstants(0, 4, colors[pixel], 0);
                _commands->DrawInstanced(3, 1, 0, 0);
            }
            // Pixel 4 remains cleared, exercising an empty overlay too.
            _compositor.Composite(_commands.Get(), target, scale);
            D3D12_RESOURCE_BARRIER barrier {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = _target.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
            _commands->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION destination {};
            destination.pResource       = _readback.Get();
            destination.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = _footprint;
            D3D12_TEXTURE_COPY_LOCATION source {};
            source.pResource = _target.Get();
            source.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            _commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            _commands->ResourceBarrier(1, &barrier);
            Check(_commands->Close(), "Close commands");
            ID3D12CommandList *lists[] = {_commands.Get()};
            _queue->ExecuteCommandLists(1, lists);
            Check(_queue->Signal(_fence.Get(), ++_fenceValue), "Signal readback fence");
            // A null event blocks until completion and avoids owning a Win32 handle.
            Check(_fence->SetEventOnCompletion(_fenceValue, nullptr), "Wait for WARP readback");
            Check(_device->GetDeviceRemovedReason(), "WARP device status");
            void *mapped = nullptr;
            const D3D12_RANGE range {static_cast<SIZE_T>(_footprint.Offset), static_cast<SIZE_T>(_footprint.Offset) + sizeof(uint16_t) * 20};
            Check(_readback->Map(0, &range, &mapped), "Map FP16 readback");
            const auto *halves = reinterpret_cast<const uint16_t *>(static_cast<const unsigned char *>(mapped) + _footprint.Offset);
            for (int pixel = 0; pixel < 5; ++pixel) {
                for (int channel = 0; channel < 4; ++channel) {
                    pixels[pixel][channel] = DirectX::PackedVector::XMConvertHalfToFloat(halves[pixel * 4 + channel]);
                }
            }
            const D3D12_RANGE noWrites {0, 0};
            _readback->Unmap(0, &noWrites);
        }
    };
} // namespace D3D12UICompositorTests

MODULE(d3d12_ui_compositor, {
    IT("scales linear HDR UI RGB while preserving coverage, scene light and repeated scale changes on WARP", {
        bool passed = true;
        try {
            D3D12UICompositorTests::RenderFixture fixture;
            // Analytic sRGB reference values include the linear toe (10/255),
            // gamma-decoded midtones, and straight color recovered from alpha.
            const float opaque[]      = {0.2158605f, 0.05126946f, 0.00303527f};
            const float translucent[] = {1.0f, 0.21404114f, 0.05087609f};
            const float scene[]       = {0.25f, 0.5f, 2.0f, 0.25f};
            constexpr float alpha     = 128.0f / 255;
            for (float scale : {1.0f, 5.0f, 1.0f}) {
                float actual[5][4];
                fixture.Render(scale, actual);
                float expected[5][4] {};
                for (int channel = 0; channel < 3; ++channel) {
                    expected[0][channel] = scale;
                    expected[1][channel] = opaque[channel] * scale;
                    expected[2][channel] = translucent[channel] * alpha * scale + scene[channel] * (1 - alpha);
                }
                expected[0][3] = expected[1][3] = 1;
                expected[2][3]                  = alpha + scene[3] * (1 - alpha);
                for (int channel = 0; channel < 4; ++channel) {
                    expected[3][channel] = expected[4][channel] = scene[channel];
                }
                for (int pixel = 0; pixel < 5; ++pixel) {
                    for (int channel = 0; channel < 4; ++channel) {
                        // FP16 output allows rounding but cannot hide clipping,
                        // alpha scaling, nonlinear scaling, or scene scaling.
                        if (!std::isfinite(actual[pixel][channel]) || std::abs(actual[pixel][channel] - expected[pixel][channel]) > 0.004f) {
                            printf("scale %.1f pixel %d channel %d: expected %.6f, got %.6f\n", scale, pixel, channel, expected[pixel][channel], actual[pixel][channel]);
                            passed = false;
                        }
                    }
                }
            }
        }
        catch (const std::exception &error) {
            printf("HDR compositor GPU regression could not complete: %s\n", error.what());
            passed = false;
        }
        EQUALS(passed, true);
    });
});
