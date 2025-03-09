#pragma once

#include <utils/safe_win32.h>
#include <d3d11.h>
#include <wrl/client.h>

#define ENABLE_MSAA 0

namespace ultralight {

    class SwapChainD3D11;

    class GPUContextD3D11 {
      public:
        GPUContextD3D11(ID3D11Device *device, ID3D11DeviceContext *immediate_context, IDXGISwapChain *swap_chain);
        virtual void Init();

        virtual ~GPUContextD3D11();

        virtual void BeginDrawing();

        virtual void EndDrawing();

        virtual ID3D11Device *device();

        virtual ID3D11DeviceContext *immediate_context();
        virtual ID3D11DeviceContext *deferred_context();
        virtual ID3D11DeviceContext *context();
        virtual IDXGISwapChain *swap_chain();

        virtual void EnableBlend();
        virtual void DisableBlend();

        virtual void EnableScissor();
        virtual void DisableScissor();

      private:
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate_context_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred_context_;
        Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;

        Microsoft::WRL::ComPtr<ID3D11BlendState> blend_state_;
        Microsoft::WRL::ComPtr<ID3D11BlendState> disabled_blend_state_;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_state_;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> scissored_rasterizer_state_;
    };

} // namespace ultralight
