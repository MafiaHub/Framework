/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include "d3d8_view_surface.h"
#include "graphics/renderer.h"
#include "gui/view.h"

namespace Framework::GUI {
    // CPU paint path into a managed IDirect3DTexture8, composited as a pre-transformed quad.
    // Render() must run between BeginScene and EndScene on the game's render thread.
    class ViewD3D8 final: public View {
      private:
        D3D8ViewSurface _surface;

      public:
        ViewD3D8(int id, Graphics::Renderer *graphicsRenderer, Manager *manager);

        [[nodiscard]] Utils::Result<void, Framework::Error> Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated = false) override;

        void Render() override;

        [[nodiscard]] void *GetNativeTexture() const override {
            return _surface.GetTexture();
        }

      private:
        bool UploadPixels(Graphics::D3D8::Device *device);
    };
} // namespace Framework::GUI
