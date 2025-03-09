/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <d3d11.h>
#include <function2.hpp>
#include <map>
#include <mutex>
#include <string>

#include <Ultralight/Ultralight.h>

#include <glm/glm.hpp>

#include "gui/sdk.h"
#include "graphics/renderer.h"

#include "gui/view.h"

namespace Framework::GUI {

    class ViewD3D11 final : public View {
      protected:
        // CPU renderer
        ID3D11Texture2D *_texture              = nullptr;
        ID3D11ShaderResourceView *_textureView = nullptr;
        
        // GPU renderer
        ultralight::GPUState _gpuState {};
        std::vector<ultralight::Vertex_2f_4ub_2f_2f_28f> _vertices {};
        std::vector<ultralight::IndexType> _indices {};
        bool _needsUpdate = true;
        uint32_t _geometryID = 0;
        ultralight::GPUState _gpuCursorState {};
        uint32_t _cursorTextureID = 0;
        ultralight::RefPtr<ultralight::Bitmap> _cursorBitmap;

      private:
        bool _d3dInitialized = false;

      private:
        void OnChangeCursor(ultralight::View *caller, ultralight::Cursor cursor) override;

        // CPU renderer
        void InitD3D();
        void ResetTextures();
        void LoadCursorData(ultralight::Cursor cursor);

        // GPU renderer
        void UpdateGeometry();

      public:
        ViewD3D11(ultralight::RefPtr<ultralight::Renderer>, Graphics::Renderer*);

        bool Init(std::string &, int, int, bool gpu_accelerated = false) override;

        void Update() override;
        void Render() override;
        void RenderCursor();

        static void InitRenderer(Framework::Graphics::Renderer *);
        static void UpdateRenderer();
    };
} // namespace Framework::GUI
