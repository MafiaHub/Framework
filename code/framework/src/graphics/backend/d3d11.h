/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "backend.h"

#include <map>

#ifndef ENABLE_MSAA
#define ENABLE_MSAA 0
#endif

#ifdef WIN32
#include <d3d11.h>
#include <wrl/client.h>
#include <directxcolors.h>
#include <d3dcompiler.h>
#else
#define ID3D11Device        void
#define ID3D11DeviceContext void
#endif

namespace Framework::Graphics {
    class D3D11Backend: public Backend<ID3D11Device *, ID3D11DeviceContext *, IDXGISwapChain *, void *> {
      public:
        bool Init(ID3D11Device *, ID3D11DeviceContext *, IDXGISwapChain *, void *) override;
        bool Shutdown() override;
        void Update() override;

        void BeginDrawing() override;
        void EndDrawing() override;
        void BindTexture(uint8_t texture_unit, uint32_t texture_id) override;
        void BindRenderBuffer(uint32_t render_buffer_id) override;
        void ClearRenderBuffer(uint32_t render_buffer_id) override;
        void DrawGeometry(uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const GPUState &state) override;
        void CreateTexture(uint32_t texture_id, Bitmap bitmap) override;
        void UpdateTexture(uint32_t texture_id, Bitmap bitmap) override;
        void DestroyTexture(uint32_t texture_id) override;
        void CreateRenderBuffer(uint32_t render_buffer_id, const RenderBuffer &buffer) override;
        void DestroyRenderBuffer(uint32_t render_buffer_id) override;
        void CreateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices) override;
        void UpdateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices) override;
        void DestroyGeometry(uint32_t geometry_id) override;

        void SetViewport(uint32_t width, uint32_t height) override;
        glm::mat4 ApplyProjection(const glm::mat4 &transform, float screen_width, float screen_height) override;

        virtual ID3D11DeviceContext *GetImmediateContext() const;
        virtual ID3D11DeviceContext *GetDeferredContext() const;
        virtual ID3D11DeviceContext *GetContext() const;

        virtual void EnableBlend();
        virtual void DisableBlend();

        virtual void EnableScissor();
        virtual void DisableScissor();

        IDXGISwapChain *GetSwapChain() const;

      private:
        void LoadCompiledVertexShader(unsigned char *data, unsigned int len, ID3D11VertexShader **ppVertexShader, const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, ID3D11InputLayout **ppInputLayout);
        void LoadCompiledPixelShader(unsigned char *data, unsigned int len, ID3D11PixelShader **ppPixelShader);
        void LoadShaders();
        void BindShader(ShaderType shader);
        void BindVertexLayout(VertexBufferFormat format);
        void BindGeometry(uint32_t id);
        ID3D11RenderTargetView *GetRenderTargetView(uint32_t render_buffer_id);
        Microsoft::WRL::ComPtr<ID3D11SamplerState> GetSamplerState();
        Microsoft::WRL::ComPtr<ID3D11Buffer> GetConstantBuffer();
        void UpdateConstantBuffer(const GPUState &state);
        static DirectX::XMMATRIX ConvertGLMMatrixToXMMatrix(const glm::mat4 &glmMatrix);
        
      protected:
        IDXGISwapChain *_swapChain {};
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _immediateContext;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _deferredContext;

        Microsoft::WRL::ComPtr<ID3D11BlendState> _blendState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> _disabledBlendState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rsState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> _scissoredRsState;

        Microsoft::WRL::ComPtr<ID3D11InputLayout> _vertex_layout_2f_4ub_2f;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> _vertex_layout_2f_4ub_2f_2f_28f;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> _samplerState;
        Microsoft::WRL::ComPtr<ID3D11Buffer> _constantBuffer;

        struct GeometryEntry {
            VertexBufferFormat format;
            Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
            Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
        };
        typedef std::map<uint32_t, GeometryEntry> GeometryMap;
        GeometryMap _geometry;

        struct TextureEntry {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture_srv;

            // These members are only used when MSAA is enabled
            bool is_msaa_render_target = false;
            bool needs_resolve         = false;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> resolve_texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> resolve_texture_srv;
        };

        typedef std::map<uint32_t, TextureEntry> TextureMap;
        TextureMap _textures;

        struct RenderTargetEntry {
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_view;
            uint32_t render_target_texture_id;
        };

        typedef std::map<uint32_t, RenderTargetEntry> RenderTargetMap;
        RenderTargetMap _renderTargets;

        typedef std::map<ShaderType, std::pair<Microsoft::WRL::ComPtr<ID3D11VertexShader>, Microsoft::WRL::ComPtr<ID3D11PixelShader>>> ShaderMap;
        ShaderMap _shaders;

    public:
        TextureEntry &GetTexture(uint32_t texture_id);

    private:
        struct Vertex_2f_4ub_2f {
            DirectX::XMFLOAT2 pos;
            uint8_t color[4];
            DirectX::XMFLOAT2 obj;
        };

        struct Vertex_2f_4ub_2f_2f_28f {
            DirectX::XMFLOAT2 pos;
            uint8_t color[4];
            DirectX::XMFLOAT2 tex;
            DirectX::XMFLOAT2 obj;
            DirectX::XMFLOAT4 data_0;
            DirectX::XMFLOAT4 data_1;
            DirectX::XMFLOAT4 data_2;
            DirectX::XMFLOAT4 data_3;
            DirectX::XMFLOAT4 data_4;
            DirectX::XMFLOAT4 data_5;
            DirectX::XMFLOAT4 data_6;
        };

        struct Uniforms {
            DirectX::XMFLOAT4 State;
            DirectX::XMMATRIX Transform;
            DirectX::XMFLOAT4 Scalar4[2];
            DirectX::XMFLOAT4 Vector[8];
            uint32_t ClipSize;
            DirectX::XMMATRIX Clip[8];
        };
    };
} // namespace Framework::Graphics
