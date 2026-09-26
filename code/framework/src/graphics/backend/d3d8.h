/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "backend.h"
#include "d3d8_api.h"

namespace Framework::Graphics {
    // Borrowed IDirect3DDevice8. Direct3D 8 has no swap chain object to query, so the back buffer
    // is read off the device itself.
    class D3D8Backend final: public Backend<D3D8::Device *, void *, void *, void *> {
      public:
        bool Init(const Framework::Graphics::RendererConfiguration &opts) override;
        void Shutdown() override;
        void Update() override;

        // False while the device is lost or waiting for the game's Reset.
        bool IsDeviceReady() const;
        bool GetBackBufferSize(int &width, int &height) const;

        // The command-list renderer is not implemented on Direct3D 8; only web views draw here.
        void BeginDrawing() override {}
        void EndDrawing() override {}
        void BindTexture(uint8_t, uint32_t) override {}
        void BindRenderBuffer(uint32_t) override {}
        void ClearRenderBuffer(uint32_t) override {}
        void DrawGeometry(uint32_t, uint32_t, uint32_t, const GPUState &) override {}
        void CreateTexture(uint32_t, Bitmap) override {}
        void UpdateTexture(uint32_t, Bitmap) override {}
        void DestroyTexture(uint32_t) override {}
        void CreateRenderBuffer(uint32_t, const RenderBuffer &) override {}
        void DestroyRenderBuffer(uint32_t) override {}
        void CreateGeometry(uint32_t, const VertexBuffer &, const IndexBuffer &) override {}
        void UpdateGeometry(uint32_t, const VertexBuffer &, const IndexBuffer &) override {}
        void DestroyGeometry(uint32_t) override {}
        void SetViewport(uint32_t, uint32_t) override {}
        glm::mat4 ApplyProjection(const glm::mat4 &, float, float) override {
            return {};
        }
    };
} // namespace Framework::Graphics
