/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <d3d9.h>
#include <wrl/client.h>

#include "graphics/renderer.h"
#include "gui/view.h"

namespace Framework::GUI {
    class ViewD3D9 final: public View {
      private:
        // CPU path only: CEF shared textures are D3D11 resources, unusable on D3D9
        Microsoft::WRL::ComPtr<IDirect3DTexture9> _texture;
        int _textureWidth  = 0;
        int _textureHeight = 0;

      public:
        ViewD3D9(int id, Graphics::Renderer *graphicsRenderer, Manager *manager);

        [[nodiscard]] Utils::Result<void, Framework::Error> Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated = false) override;

        void Update() override;
        void Render() override;

      private:
        bool UploadPixels(IDirect3DDevice9 *device);
        void DrawQuad(IDirect3DDevice9 *device) const;
    };
} // namespace Framework::GUI
