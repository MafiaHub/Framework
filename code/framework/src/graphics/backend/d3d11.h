/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "backend.h"

#ifdef WIN32
#include <d3d11.h>
#include <wrl/client.h>
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

        virtual ID3D11DeviceContext *GetImmediateContext();
        virtual ID3D11DeviceContext *GetDeferredContext();
        virtual ID3D11DeviceContext *GetContext();

        virtual void EnableBlend();
        virtual void DisableBlend();

        virtual void EnableScissor();
        virtual void DisableScissor();

        IDXGISwapChain* GetSwapChain() const {
            return _swapChain;
        }

      protected:
        IDXGISwapChain *_swapChain {};
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _immediateContext;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> _deferredContext;

        Microsoft::WRL::ComPtr<ID3D11BlendState> _blendState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> _disabledBlendState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rsState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> _scissoredRsState;
    };
} // namespace Framework::Graphics
