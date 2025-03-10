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
#include <d3d9.h>
#else
#define IDirect3D9 void
#endif

namespace Framework::Graphics {
    class D3D9Backend: public Backend<IDirect3DDevice9 *, void *, void *, void *> {
      public:
        bool Init(const Framework::Graphics::RendererConfiguration &opts) override;
        bool Shutdown() override;
        void Update() override;

        // TODO: Backend not implemented yet
        void BeginDrawing() {}
        void EndDrawing() {}
        void BindTexture(uint8_t texture_unit, uint32_t texture_id) {}
        void BindRenderBuffer(uint32_t render_buffer_id) {}
        void ClearRenderBuffer(uint32_t render_buffer_id) {}
        void DrawGeometry(uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const GPUState &state) {}
        void CreateTexture(uint32_t texture_id, Bitmap bitmap) {};
        void UpdateTexture(uint32_t texture_id, Bitmap bitmap) {};
        void DestroyTexture(uint32_t texture_id) {};
        void CreateRenderBuffer(uint32_t render_buffer_id, const RenderBuffer &buffer) {};
        void DestroyRenderBuffer(uint32_t render_buffer_id) {};
        void CreateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices) {};
        void UpdateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices) {};
        void DestroyGeometry(uint32_t geometry_id) {};
        void SetViewport(uint32_t width, uint32_t height) {};
        glm::mat4 ApplyProjection(const glm::mat4 &transform, float screen_width, float screen_height) {
            return {};
        };
    };
} // namespace Framework::Graphics
