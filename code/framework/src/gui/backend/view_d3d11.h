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
        uint32_t _renderTextureID = 0;

        // GPU renderer
        ultralight::GPUState _gpuState {};
        std::vector<ultralight::Vertex_2f_4ub_2f_2f_28f> _vertices {};
        std::vector<ultralight::IndexType> _indices {};
        bool _needsUpdate = true;
        uint32_t _geometryID = 0;

      private:
        bool _d3dInitialized = false;

      private:
        // CPU renderer
        void InitD3D();
        void ResetTextures();

        // GPU renderer
        void UpdateGeometry();

      public:
        ViewD3D11(ultralight::RefPtr<ultralight::Renderer>, Graphics::Renderer*);

        bool Init(std::string &, int, int, int, int, bool gpu_accelerated = false) override;

        void Update() override;
        void Render() override;

        static void InitRenderer(Framework::Graphics::Renderer *);
    };
} // namespace Framework::GUI
