/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include "graphics/backend/d3d8_api.h"

#include <cstddef>
#include <cstdint>

namespace Framework::GUI {
    // A view's managed IDirect3DTexture8 and the pre-transformed quad that composites it, fed from
    // CPU BGRA pixels a dirty region at a time. Shared by the in-process D3D8 view and the remote
    // one, which differ only in where the pixels come from. Render thread only.
    class D3D8ViewSurface final {
      private:
        Graphics::D3D8::Texture *_texture = nullptr;
        // Power-of-two storage: Direct3D 8 hardware may reject other sizes.
        int _textureWidth  = 0;
        int _textureHeight = 0;
        // View size the texture contents were uploaded for.
        int _contentWidth  = 0;
        int _contentHeight = 0;

      public:
        D3D8ViewSurface() = default;
        ~D3D8ViewSurface();

        D3D8ViewSurface(const D3D8ViewSurface &)            = delete;
        D3D8ViewSurface &operator=(const D3D8ViewSurface &) = delete;

        void Release();

        Graphics::D3D8::Texture *GetTexture() const {
            return _texture;
        }

        // Holds a whole frame of a width x height view.
        bool HasContent(int width, int height) const {
            return _texture != nullptr && _contentWidth == width && _contentHeight == height;
        }

        // Makes the texture fit a width x height view. |fullUpload| is set when what it holds no
        // longer matches the view, so the next upload must cover all of it.
        bool Prepare(Graphics::D3D8::Device *device, int width, int height, int viewId, bool &fullUpload);

        // Copies |region| of a width x height BGRA surface; the rest of the texture keeps what it had.
        bool Upload(const uint8_t *pixels, std::size_t pitch, int width, int height, const RECT &region);

        // Must run between BeginScene and EndScene.
        void Draw(Graphics::D3D8::Device *device, int x, int y, int width, int height) const;
    };
} // namespace Framework::GUI
