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
        ID3D11Texture2D *_texture              = nullptr;
        ID3D11ShaderResourceView *_textureView = nullptr;

      private:
        bool _d3dInitialized = false;

      private:
        void OnChangeCursor(ultralight::View *caller, ultralight::Cursor cursor) override;

        void InitD3D();
        void ResetTextures();
        void LoadCursorData(ultralight::Cursor cursor);

      public:
        ViewD3D11(ultralight::RefPtr<ultralight::Renderer>, Graphics::Renderer*);

        bool Init(std::string &, int, int) override;

        void Update() override;
        void Render() override;
        void RenderCursor();
    };
} // namespace Framework::GUI
