/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d11.h"

#include <string>
#include <sstream>
#include <directxcolors.h>
#include <d3dcompiler.h>

#include "graphics/shaders/hlsl/bin/fill_fxc.h"
#include "graphics/shaders/hlsl/bin/fill_path_fxc.h"
#include "graphics/shaders/hlsl/bin/v2f_c4f_t2f_fxc.h"
#include "graphics/shaders/hlsl/bin/v2f_c4f_t2f_t2f_d28f_fxc.h"

#include <glm/gtc/matrix_transform.hpp>

#include <logging/logger.h>

namespace Framework::Graphics {
    using namespace Microsoft::WRL;

    bool D3D11Backend::Init(const Framework::Graphics::RendererConfiguration &opts) {
        _device    = opts.d3d11.device;
        _context   = opts.d3d11.deviceContext;
        _swapChain = opts.d3d11.swapChain;

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

        if (opts.d3d11.useDeferredContext)
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

    void D3D11Backend::CreateTexture(uint32_t texture_id, Bitmap bitmap) {
        auto i = _textures.find(texture_id);
        if (i != _textures.end()) {
            MessageBoxW(nullptr, L"D3D11Backend::CreateTexture, texture id already exists.", L"Error", MB_OK);
            return;
        }

        if (bitmap.format != BitmapFormat::BGRA8 && bitmap.format != BitmapFormat::A8)
            MessageBoxW(nullptr, L"D3D11Backend::CreateTexture, unsupported format.", L"Error", MB_OK);

        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width     = bitmap.width;
        desc.Height    = bitmap.height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format                     = bitmap.format == BitmapFormat::BGRA8 ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_A8_UNORM;
        desc.SampleDesc.Count           = 1;
        desc.Usage                      = D3D11_USAGE_DYNAMIC;
        desc.BindFlags                  = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags             = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags                  = 0;

        auto &texture_entry = _textures[texture_id];
        HRESULT hr;

        if (bitmap.pixels == nullptr) {
            desc.BindFlags      = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            desc.Usage          = D3D11_USAGE_DEFAULT;
            desc.CPUAccessFlags = 0;
#if ENABLE_MSAA
            desc.SampleDesc.Count   = 8;
            desc.SampleDesc.Quality = D3D11_STANDARD_MULTISAMPLE_PATTERN;

            texture_entry.is_msaa_render_target = true;
#endif

            hr = _device->CreateTexture2D(&desc, nullptr, texture_entry.texture.GetAddressOf());
        }
        else {
            if (bitmap.size == 0 && bitmap.pixels == nullptr)
                throw std::runtime_error(fmt::format("D3D11Backend::CreateTexture fault, size:{} ptr:{}", bitmap.size, (void *)bitmap.pixels));
            D3D11_SUBRESOURCE_DATA tex_data;
            ZeroMemory(&tex_data, sizeof(tex_data));
            tex_data.pSysMem          = bitmap.pixels;
            tex_data.SysMemPitch      = bitmap.pitch;
            tex_data.SysMemSlicePitch = (UINT)bitmap.size;

            hr = _device->CreateTexture2D(&desc, &tex_data, texture_entry.texture.GetAddressOf());
        }

        if (FAILED(hr)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D11Backend::CreateTexture, unable to create texture. hr:{}, w:{}, h:{}, size:{}", hr, desc.Width, desc.Height, bitmap.size);
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
        ZeroMemory(&srv_desc, sizeof(srv_desc));
        srv_desc.Format                    = desc.Format;
        srv_desc.ViewDimension             = texture_entry.is_msaa_render_target ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MostDetailedMip = 0;
        srv_desc.Texture2D.MipLevels       = 1;

        hr = _device->CreateShaderResourceView(texture_entry.texture.Get(), &srv_desc, texture_entry.texture_srv.GetAddressOf());

#if ENABLE_MSAA
        if (FAILED(hr)) {
            if (texture_entry.is_msaa_render_target) {
                // Create resolve texture and shader resource view

                desc.SampleDesc.Count   = 1;
                desc.SampleDesc.Quality = 0;
                hr                      = _device->CreateTexture2D(&desc, NULL, texture_entry.resolve_texture.GetAddressOf());

                if (FAILED(hr))
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D11Backend::CreateTexture, unable to create MSAA resolve texture.");

                srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

                hr = _device->CreateShaderResourceView(texture_entry.resolve_texture.Get(), &srv_desc, texture_entry.resolve_texture_srv.GetAddressOf());

                if (FAILED(hr))
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)
                        ->error("D3D11Backend::CreateTexture, unable to create shader resource view for MSAA ");
        }
#endif
    }

    void D3D11Backend::UpdateTexture(uint32_t texture_id, Bitmap bitmap) {
        auto i = _textures.find(texture_id);
        if (i == _textures.end()) {
            return;
        }

        auto &entry = i->second;
        D3D11_MAPPED_SUBRESOURCE res;
        GetContext()->Map(entry.texture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &res);

        if (res.RowPitch == bitmap.pitch) {
            memcpy(res.pData, bitmap.pixels, bitmap.size);
        }
        else {
            Bitmap mapped_bitmap;
            mapped_bitmap.width = bitmap.width;
            mapped_bitmap.height = bitmap.height;
            mapped_bitmap.format = bitmap.format;
            mapped_bitmap.pitch  = res.RowPitch;
            mapped_bitmap.size  = res.RowPitch * bitmap.height;
            mapped_bitmap.pixels = (uint8_t *)res.pData;

            IntRect dest_rect = {0, 0, (int)bitmap.width, (int)bitmap.height};
            mapped_bitmap.DrawBitmap(dest_rect, dest_rect, bitmap, false);
        }

        GetContext()->Unmap(entry.texture.Get(), 0);
    }

    void D3D11Backend::DestroyTexture(uint32_t texture_id) {
        auto i = _textures.find(texture_id);
        if (i != _textures.end())
            _textures.erase(i);
    }

    D3D11Backend::TextureEntry& D3D11Backend::GetTexture(uint32_t texture_id) {
        auto i = _textures.find(texture_id);
        if (i == _textures.end()) {
            throw new std::runtime_error("D3D11Backend::GetTexture, texture id doesn't exist.");
        }

        return i->second;
    }

    void D3D11Backend::CreateRenderBuffer(uint32_t render_buffer_id, const RenderBuffer& buffer) {
        if (render_buffer_id == 0) {
            throw new std::runtime_error("D3D11Backend::CreateRenderBuffer, render buffer ID 0 is reserved for default ");
            return;
        }

        auto i = _renderTargets.find(render_buffer_id);
        if (i != _renderTargets.end()) {
            throw new std::runtime_error("D3D11Backend::CreateRenderBuffer, render buffer id already exists.");
            return;
        }

        auto tex_entry = _textures.find(buffer.texture_id);
        if (tex_entry == _textures.end()) {
            throw new std::runtime_error("D3D11Backend::CreateRenderBuffer, texture id doesn't exist.");
            return;
        }

        D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
        ZeroMemory(&renderTargetViewDesc, sizeof(renderTargetViewDesc));
        renderTargetViewDesc.Format        = DXGI_FORMAT_B8G8R8A8_UNORM;
        renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
#if ENABLE_MSAA
        renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
#endif

        ComPtr<ID3D11Texture2D> tex = tex_entry->second.texture;
        auto &render_target_entry   = _renderTargets[render_buffer_id];
        HRESULT hr                  = _device->CreateRenderTargetView(tex.Get(), &renderTargetViewDesc, render_target_entry.render_target_view.GetAddressOf());

        render_target_entry.render_target_texture_id = buffer.texture_id;
        if (FAILED(hr))
            throw new std::runtime_error("D3D11Backend::CreateRenderBuffer, unable to create render target.");
    }

    void D3D11Backend::DestroyRenderBuffer(uint32_t render_buffer_id) {
        auto i = _renderTargets.find(render_buffer_id);
        if (i != _renderTargets.end()) {
            i->second.render_target_view.Reset();
            _renderTargets.erase(i);
        }
    }

    void D3D11Backend::CreateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices) {
        BindVertexLayout(vertices.format);

        if (_geometry.find(geometry_id) != _geometry.end())
            return;

        GeometryEntry geometry;
        geometry.format = vertices.format;

        HRESULT hr;

        D3D11_BUFFER_DESC vertex_desc;
        ZeroMemory(&vertex_desc, sizeof(vertex_desc));
        vertex_desc.Usage          = D3D11_USAGE_DYNAMIC;
        vertex_desc.ByteWidth      = vertices.size;
        vertex_desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        vertex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA vertex_data;
        ZeroMemory(&vertex_data, sizeof(vertex_data));
        vertex_data.pSysMem = vertices.data;

        hr = _device->CreateBuffer(&vertex_desc, &vertex_data, geometry.vertexBuffer.GetAddressOf());
        if (FAILED(hr))
            return;

        D3D11_BUFFER_DESC index_desc;
        ZeroMemory(&index_desc, sizeof(index_desc));
        index_desc.Usage          = D3D11_USAGE_DYNAMIC;
        index_desc.ByteWidth      = indices.size;
        index_desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
        index_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA index_data;
        ZeroMemory(&index_data, sizeof(index_data));
        index_data.pSysMem = indices.data;

        hr = _device->CreateBuffer(&index_desc, &index_data, geometry.indexBuffer.GetAddressOf());
        if (FAILED(hr)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->error("D3D11Backend::CreateBuffer, could not create a geometry!");
            return;
        }

        _geometry.insert({geometry_id, std::move(geometry)});
    }

    void D3D11Backend::UpdateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices) {
        auto i = _geometry.find(geometry_id);
        if (i == _geometry.end()) {
            return;
        }

        auto &entry = i->second;
        D3D11_MAPPED_SUBRESOURCE res;

        GetContext()->Map(entry.vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
        memcpy(res.pData, vertices.data, vertices.size);
        GetContext()->Unmap(entry.vertexBuffer.Get(), 0);

        GetContext()->Map(entry.indexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
        memcpy(res.pData, indices.data, indices.size);
        GetContext()->Unmap(entry.indexBuffer.Get(), 0);
    }

    void D3D11Backend::DestroyGeometry(uint32_t geometry_id) {
        auto i = _geometry.find(geometry_id);
        if (i != _geometry.end()) {
            i->second.vertexBuffer.Reset();
            i->second.indexBuffer.Reset();
            _geometry.erase(i);
        }
    }

    void D3D11Backend::BindTexture(uint8_t texture_unit, uint32_t texture_id) {
        auto i = _textures.find(texture_id);
        if (i == _textures.end()) {
            return;
        }

        auto &entry = i->second;

        if (entry.is_msaa_render_target) {
            if (entry.needs_resolve) {
                GetContext()->ResolveSubresource(entry.resolve_texture.Get(), 0, entry.texture.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
                entry.needs_resolve = false;
            }

            GetContext()->PSSetShaderResources(texture_unit, 1, entry.resolve_texture_srv.GetAddressOf());
        }
        else {
            GetContext()->PSSetShaderResources(texture_unit, 1, entry.texture_srv.GetAddressOf());
        }
    }

    void D3D11Backend::BindRenderBuffer(uint32_t render_buffer_id) {
        // Unbind any textures/shader resources to avoid warnings in case a render
        // buffer that we would like to bind is already bound as an input texture.
        ID3D11ShaderResourceView *nullSRV[1] = {nullptr};
        GetContext()->PSSetShaderResources(0, 1, nullSRV);
        GetContext()->PSSetShaderResources(1, 1, nullSRV);
        GetContext()->PSSetShaderResources(2, 1, nullSRV);

        if (render_buffer_id == UINT32_MAX)
            return;

        ID3D11RenderTargetView *target = GetRenderTargetView(render_buffer_id);
        if (!target) {
            return;
        }

        GetContext()->OMSetRenderTargets(1, &target, nullptr);
    }

    void D3D11Backend::ClearRenderBuffer(uint32_t render_buffer_id) {
        if (render_buffer_id == UINT32_MAX)
            return;

        float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        ID3D11RenderTargetView *target = GetRenderTargetView(render_buffer_id);
        if (!target) {
            return;
        }

        GetContext()->ClearRenderTargetView(target, color);
    }

    void D3D11Backend::DrawGeometry(uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const GPUState &state) {
        BindRenderBuffer(state.render_buffer_id);

        SetViewport(state.viewport_width, state.viewport_height);

        if (state.texture_1_id)
            BindTexture(0, state.texture_1_id);

        if (state.texture_2_id)
            BindTexture(1, state.texture_2_id);

        UpdateConstantBuffer(state);

        BindGeometry(geometry_id);

        auto ctx = GetContext();

        auto sampler_state = GetSamplerState();
        ctx->PSSetSamplers(0, 1, sampler_state.GetAddressOf());

        BindShader(state.shader_type);

        if (state.enable_blend)
            EnableBlend();
        else
            DisableBlend();

        if (state.enable_scissor) {
            EnableScissor();
            D3D11_RECT scissor_rect = {(LONG)(state.scissor_rect.left), (LONG)(state.scissor_rect.top), (LONG)(state.scissor_rect.right), (LONG)(state.scissor_rect.bottom)};

            ctx->RSSetScissorRects(1, &scissor_rect);
        }
        else {
            DisableScissor();
        }

        ctx->VSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
        ctx->PSSetConstantBuffers(0, 1, _constantBuffer.GetAddressOf());
        ctx->DrawIndexed(indices_count, indices_offset, 0);
        _batchCount++;
    }

    ID3D11DeviceContext *D3D11Backend::GetImmediateContext() const {
        return _context;
    }

    ID3D11DeviceContext *D3D11Backend::GetDeferredContext() const {
        return _deferredContext.Get();
    }

    ID3D11DeviceContext *D3D11Backend::GetContext() const {
        return _deferredContext != nullptr ? GetDeferredContext() : GetImmediateContext();
    }

    IDXGISwapChain *D3D11Backend::GetSwapChain() const {
        return _swapChain;
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

    void D3D11Backend::BeginDrawing() {

    }
    void D3D11Backend::EndDrawing() {

    }

    void D3D11Backend::LoadCompiledVertexShader(unsigned char *data, unsigned int len, ID3D11VertexShader **ppVertexShader, const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, ID3D11InputLayout **ppInputLayout) {
        HRESULT hr;

        // Create the vertex shader
        hr = _device->CreateVertexShader(data, len, nullptr, ppVertexShader);

        if (FAILED(hr)) {
            throw new std::runtime_error("D3D11Backend::LoadCompiledVertexShader, Vertex shader could not be compiled.Check ");
        }

        // Create the input layout
        hr = _device->CreateInputLayout(pInputElementDescs, NumElements, data, len, ppInputLayout);

        if (FAILED(hr)) {
            throw new std::runtime_error("D3D11Backend::LoadCompiledVertexShader, Could not create vertex input layout.");
        }
    }

    void D3D11Backend::LoadCompiledPixelShader(unsigned char *data, unsigned int len, ID3D11PixelShader **ppPixelShader) {
        HRESULT hr;

        // Create the pixel shader
        hr = _device->CreatePixelShader(data, len, nullptr, ppPixelShader);

        if (FAILED(hr)) {
            throw new std::runtime_error("D3D11Backend::LoadCompiledPixelShader, Pixel shader could not be compiled.");
        }
    }

    void D3D11Backend::LoadShaders() {
        if (!_shaders.empty())
            return;

        const D3D11_INPUT_ELEMENT_DESC layout_2f_4ub_2f[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        auto &shader_fill_path = _shaders[ShaderType::FillPath];
        LoadCompiledVertexShader(v2f_c4f_t2f_fxc, v2f_c4f_t2f_fxc_len, shader_fill_path.first.GetAddressOf(), layout_2f_4ub_2f, ARRAYSIZE(layout_2f_4ub_2f), _vertex_layout_2f_4ub_2f.GetAddressOf());
        LoadCompiledPixelShader(fill_path_fxc, fill_path_fxc_len, shader_fill_path.second.GetAddressOf());

        //

        const D3D11_INPUT_ELEMENT_DESC layout_2f_4ub_2f_2f_28f[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 7, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        };

        auto &shader_fill = _shaders[ShaderType::Fill];
        LoadCompiledVertexShader(v2f_c4f_t2f_t2f_d28f_fxc, v2f_c4f_t2f_t2f_d28f_fxc_len, shader_fill.first.GetAddressOf(), layout_2f_4ub_2f_2f_28f, ARRAYSIZE(layout_2f_4ub_2f_2f_28f), _vertex_layout_2f_4ub_2f_2f_28f.GetAddressOf());
        LoadCompiledPixelShader(fill_fxc, fill_fxc_len, shader_fill.second.GetAddressOf());
    }

    void D3D11Backend::BindShader(ShaderType shader) {
        LoadShaders();

        ShaderType shader_type = (ShaderType)shader;
        switch (shader_type) {
        case ShaderType::Fill: {
            auto &shader = _shaders[ShaderType::Fill];
            GetContext()->VSSetShader(shader.first.Get(), nullptr, 0);
            GetContext()->PSSetShader(shader.second.Get(), nullptr, 0);
            break;
        }
        case ShaderType::FillPath: {
            auto &shader = _shaders[ShaderType::FillPath];
            GetContext()->VSSetShader(shader.first.Get(), nullptr, 0);
            GetContext()->PSSetShader(shader.second.Get(), nullptr, 0);
            break;
        }
        }
    }

    void D3D11Backend::BindVertexLayout(VertexBufferFormat format) {
        LoadShaders();

        switch (format) {
        case VertexBufferFormat::_2f_4ub_2f: GetContext()->IASetInputLayout(_vertex_layout_2f_4ub_2f.Get()); break;
        case VertexBufferFormat::_2f_4ub_2f_2f_28f: GetContext()->IASetInputLayout(_vertex_layout_2f_4ub_2f_2f_28f.Get()); break;
        };
    }

    void D3D11Backend::BindGeometry(uint32_t id) {
        auto i = _geometry.find(id);
        if (i == _geometry.end()) {
            throw std::runtime_error("Geometry id not found!");
        }

        auto immediate_ctx = GetContext();

        auto &geometry = i->second;
        UINT stride    = geometry.format == VertexBufferFormat::_2f_4ub_2f ? sizeof(Vertex_2f_4ub_2f) : sizeof(Vertex_2f_4ub_2f_2f_28f);
        UINT offset    = 0;
        immediate_ctx->IASetVertexBuffers(0, 1, geometry.vertexBuffer.GetAddressOf(), &stride, &offset);
        immediate_ctx->IASetIndexBuffer(geometry.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        immediate_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        BindVertexLayout(geometry.format);
    }

    ID3D11RenderTargetView *D3D11Backend::GetRenderTargetView(uint32_t render_buffer_id) {
        ID3D11RenderTargetView *target = nullptr;

        auto i = _renderTargets.find(render_buffer_id);
        if (i != _renderTargets.end()) {
            target = i->second.render_target_view.Get();

#if ENABLE_MSAA
            auto j = _textures.find(i->second.render_target_texture_id);
            if (j == _textures.end()) {
                throw new std::runtime_error("D3D11Backend::BindRenderBuffer, render target texture id doesn't exist.");
                return nullptr;
            }

            // Flag the MSAA render target texture for Resolve when we bind it to
            // a shader for reading later.
            if (j->second.is_msaa_render_target) {
                j->second.needs_resolve = true;
            }
#endif
        }
        else {
            // Couldn't find the render buffer id in our local render target map.
            ID3D11RenderTargetView *pRenderTargetView = nullptr;
            ID3D11Texture2D *pBackBuffer              = nullptr;

            if (_swapChain) {
                // Get the back buffer from the swap chain
                if (SUCCEEDED(_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&pBackBuffer))) {
                    // Create a render target view
                    _device->CreateRenderTargetView(pBackBuffer, nullptr, &target);
                    pBackBuffer->Release();
                }
            }
        }

        return target;
    }

    ComPtr<ID3D11SamplerState> D3D11Backend::GetSamplerState() {
        if (_samplerState)
            return _samplerState;

        D3D11_SAMPLER_DESC sampler_desc;
        ZeroMemory(&sampler_desc, sizeof(sampler_desc));
        sampler_desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler_desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler_desc.MinLOD         = 0;
        HRESULT hr                  = _device->CreateSamplerState(&sampler_desc, &_samplerState);

        if (FAILED(hr))
            throw new std::runtime_error("D3D11Backend::GetSamplerState, unable to create sampler state.");

        return _samplerState;
    }

    ComPtr<ID3D11Buffer> D3D11Backend::GetConstantBuffer() {
        if (_constantBuffer)
            return _constantBuffer;

        D3D11_BUFFER_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth      = sizeof(Uniforms);
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = _device->CreateBuffer(&desc, nullptr, _constantBuffer.GetAddressOf());
        if (FAILED(hr))
            throw new std::runtime_error("D3D11Backend::GetConstantBuffer, unable to create constant buffer.");

        return _constantBuffer;
    }

    void D3D11Backend::SetViewport(uint32_t width, uint32_t height) {
        D3D11_VIEWPORT vp;
        ZeroMemory(&vp, sizeof(vp));
        vp.Width    = (float)width;
        vp.Height   = (float)height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        GetContext()->RSSetViewports(1, &vp);
    }

    DirectX::XMMATRIX D3D11Backend::ConvertGLMMatrixToXMMatrix(const glm::mat4 &glmMatrix) {
        // GLM matrices are stored in column-major order, same as DirectX::XMMATRIX
        // So we can map the elements directly with proper casting

        return DirectX::XMMATRIX(
            glmMatrix[0][0], glmMatrix[0][1], glmMatrix[0][2], glmMatrix[0][3], 
            glmMatrix[1][0], glmMatrix[1][1], glmMatrix[1][2], glmMatrix[1][3], 
            glmMatrix[2][0], glmMatrix[2][1], glmMatrix[2][2], glmMatrix[2][3], 
            glmMatrix[3][0], glmMatrix[3][1], glmMatrix[3][2], glmMatrix[3][3]);
    }

    void D3D11Backend::UpdateConstantBuffer(const GPUState &state) {
        auto buffer = GetConstantBuffer();

        glm::mat4 model_view_projection = ApplyProjection(state.transform, (float)state.viewport_width, (float)state.viewport_height);

        float screen_width  = (float)state.viewport_width;
        float screen_height = (float)state.viewport_height;

        Uniforms uniforms;
        uniforms.State      = {0.0f, screen_width, screen_height, (float)1.0f};
        uniforms.Transform  = ConvertGLMMatrixToXMMatrix(model_view_projection);
        uniforms.Scalar4[0] = {state.uniform_scalar[0], state.uniform_scalar[1], state.uniform_scalar[2], state.uniform_scalar[3]};
        uniforms.Scalar4[1] = {state.uniform_scalar[4], state.uniform_scalar[5], state.uniform_scalar[6], state.uniform_scalar[7]};
        for (size_t i = 0; i < 8; ++i) uniforms.Vector[i] = DirectX::XMFLOAT4(state.uniform_vector[i].x, state.uniform_vector[i].y, state.uniform_vector[i].z, state.uniform_vector[i].w);
        uniforms.ClipSize = state.clip_size;
        for (size_t i = 0; i < state.clip_size; ++i) uniforms.Clip[i] = ConvertGLMMatrixToXMMatrix(state.clip[i]);

        D3D11_MAPPED_SUBRESOURCE res;
        GetContext()->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &res);
        memcpy(res.pData, &uniforms, sizeof(Uniforms));
        GetContext()->Unmap(buffer.Get(), 0);
    }

    glm::mat4 D3D11Backend::ApplyProjection(const glm::mat4 &transform, float screen_width, float screen_height) {
        glm::mat4 projection = glm::ortho(0.0f, // left
            screen_width,                       // right
            screen_height,                      // bottom (inverted for D3D11)
            0.0f,                               // top
            -1.0f,                              // near
            1.0f                                // far
        );

        return projection * transform;
    }
} // namespace Framework::Graphics
