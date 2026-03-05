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
#include <mutex>
#include <wrl/client.h>

#include "graphics/renderer.h"
#include "gui/view.h"

namespace Framework::GUI {
    class ViewD3D11 final: public View {
      private:
        // GPU path: texture received from CEF's OnAcceleratedPaint
        uint32_t _textureID  = 0;
        uint32_t _geometryID = 0;
        bool _geometryCreated = false;

        // CPU path: texture created from pixel data
        uint32_t _cpuTextureID = 0;

      public:
        ViewD3D11(Graphics::Renderer *graphicsRenderer, Manager *manager);

        [[nodiscard]] GUIError Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated = false) override;

        void Update() override;
        void Render() override;

      private:
        void CreateOrUpdateGeometry();
    };
} // namespace Framework::GUI
