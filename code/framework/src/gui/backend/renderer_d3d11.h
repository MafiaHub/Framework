#pragma once

#include "graphics/renderer.h"

#include <ultralight/platform/GPUDriver.h>

namespace Framework::GUI {
    class RendererD3D11: public ultralight::GPUDriver {
      private:
        Framework::Graphics::D3D11Backend *_renderer {};

      public:
        bool Init(Graphics::Renderer *);
        
        void BeginDrawing();
        void EndDrawing();
        void BindTexture(uint8_t texture_unit, uint32_t texture_id);
        void BindRenderBuffer(uint32_t render_buffer_id);
        void ClearRenderBuffer(uint32_t render_buffer_id);
        void DrawGeometry(uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const ultralight::GPUState &state);
        bool HasCommandsPending();
        void DrawCommandList();
        void BeginSynchronize() override;
        void EndSynchronize() override;
        uint32_t NextTextureId() override;
        uint32_t NextRenderBufferId() override;
        uint32_t NextGeometryId() override;
        void UpdateCommandList(const ultralight::CommandList &list) override;
        void CreateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) override;
        void UpdateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) override;
        void DestroyTexture(uint32_t texture_id) override;
        void CreateRenderBuffer(uint32_t render_buffer_id, const ultralight::RenderBuffer &buffer) override;
        void DestroyRenderBuffer(uint32_t render_buffer_id) override;
        void CreateGeometry(uint32_t geometry_id, const ultralight::VertexBuffer &vertices, const ultralight::IndexBuffer &indices) override;
        void UpdateGeometry(uint32_t geometry_id, const ultralight::VertexBuffer &vertices, const ultralight::IndexBuffer &indices) override;
        void DestroyGeometry(uint32_t geometry_id) override;

        Framework::Graphics::D3D11Backend* GetBackend() {
            return _renderer;
        }

      private:
        static Framework::Graphics::Command Convert(ultralight::Command cmd);
        static Framework::Graphics::GPUState Convert(ultralight::GPUState state);
        static Framework::Graphics::IntRect Convert(ultralight::IntRect rect);
        static Framework::Graphics::Bitmap Convert(ultralight::RefPtr<ultralight::Bitmap> bitmap);
        static Framework::Graphics::RenderBuffer Convert(ultralight::RenderBuffer renderBuffer);
        static Framework::Graphics::VertexBuffer Convert(ultralight::VertexBuffer buffer);
        static Framework::Graphics::IndexBuffer Convert(ultralight::IndexBuffer buffer);
        static void UnlockBitmap(ultralight::RefPtr<ultralight::Bitmap> bitmap); // needs to be called after Convert() !!!
        static glm::vec4 Convert(ultralight::vec4 vec);
        static glm::mat4 Convert(ultralight::Matrix4x4 mat);
    };
}
