/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "d3d8_view_surface.h"

#include "logging/logger.h"

#include <cstring>

namespace Framework::GUI {
    namespace {
        namespace D3D8 = Graphics::D3D8;

        struct Vertex {
            float x, y, z, rhw;
            DWORD color;
            float u, v;
        };
        constexpr DWORD kVertexFVF = D3D8::kFvfXyzRhw | D3D8::kFvfDiffuse | D3D8::kFvfTex1;

        int NextPowerOfTwo(int value) {
            int result = 1;
            while (result < value) {
                result <<= 1;
            }
            return result;
        }
    } // namespace

    D3D8ViewSurface::~D3D8ViewSurface() {
        Release();
    }

    void D3D8ViewSurface::Release() {
        if (_texture) {
            _texture->Release();
            _texture = nullptr;
        }
        _textureWidth  = 0;
        _textureHeight = 0;
        _contentWidth  = 0;
        _contentHeight = 0;
    }

    bool D3D8ViewSurface::Prepare(D3D8::Device *device, int width, int height, int viewId, bool &fullUpload) {
        const int wantedWidth  = NextPowerOfTwo(width);
        const int wantedHeight = NextPowerOfTwo(height);
        fullUpload             = _contentWidth != width || _contentHeight != height;
        if (!_texture || _textureWidth != wantedWidth || _textureHeight != wantedHeight) {
            Release();
            // MANAGED survives the game's Reset after a lost device, so nothing is recreated then.
            if (FAILED(device->CreateTexture(wantedWidth, wantedHeight, 1, 0, D3D8::kFormatA8R8G8B8, D3D8::kPoolManaged, &_texture)) || !_texture) {
                _texture = nullptr;
                Framework::Logging::GetLogger("Web")->error("View {}: failed to create {}x{} D3D8 texture", viewId, wantedWidth, wantedHeight);
                return false;
            }
            _textureWidth  = wantedWidth;
            _textureHeight = wantedHeight;
            fullUpload     = true;
        }
        return true;
    }

    bool D3D8ViewSurface::Upload(const uint8_t *pixels, std::size_t pitch, int width, int height, const RECT &region) {
        D3D8::LockedRect locked {};
        if (FAILED(_texture->LockRect(0, &locked, &region, 0))) {
            return false;
        }

        const std::size_t rowBytes = static_cast<std::size_t>(region.right - region.left) * 4;
        const auto *src            = pixels + static_cast<std::size_t>(region.top) * pitch + static_cast<std::size_t>(region.left) * 4;
        auto *dst                  = static_cast<uint8_t *>(locked.bits);
        for (LONG y = region.top; y < region.bottom; ++y) {
            std::memcpy(dst, src, rowBytes);
            dst += locked.pitch;
            src += pitch;
        }
        _texture->UnlockRect(0);

        _contentWidth  = width;
        _contentHeight = height;
        return true;
    }

    void D3D8ViewSurface::Draw(D3D8::Device *device, int x, int y, int width, int height) const {
        // -0.5 aligns pixel centers to texel centers, as on Direct3D 9
        const float left   = static_cast<float>(x) - 0.5f;
        const float top    = static_cast<float>(y) - 0.5f;
        const float right  = left + static_cast<float>(width);
        const float bottom = top + static_cast<float>(height);
        const float u      = static_cast<float>(width) / static_cast<float>(_textureWidth);
        const float v      = static_cast<float>(height) / static_cast<float>(_textureHeight);
        const DWORD white  = 0xffffffff;

        const Vertex vertices[4] = {
            {left, top, 0.0f, 1.0f, white, 0.0f, 0.0f},
            {right, top, 0.0f, 1.0f, white, u, 0.0f},
            {left, bottom, 0.0f, 1.0f, white, 0.0f, v},
            {right, bottom, 0.0f, 1.0f, white, u, v},
        };

        // The game caches its own render states; everything touched here is restored after.
        DWORD stateBlock = 0;
        if (FAILED(device->CreateStateBlock(D3D8::kStateBlockAll, &stateBlock))) {
            return;
        }

        const D3D8::Viewport viewport {0, 0, static_cast<DWORD>(x + width), static_cast<DWORD>(y + height), 0.0f, 1.0f};
        device->SetViewport(&viewport);
        device->SetVertexShader(kVertexFVF);
        device->SetPixelShader(0);
        device->SetTexture(0, _texture);
        device->SetTexture(1, nullptr);

        // CEF paints premultiplied BGRA.
        device->SetRenderState(D3D8::kRsAlphaBlendEnable, TRUE);
        device->SetRenderState(D3D8::kRsSrcBlend, D3D8::kBlendOne);
        device->SetRenderState(D3D8::kRsDestBlend, D3D8::kBlendInvSrcAlpha);
        device->SetRenderState(D3D8::kRsBlendOp, D3D8::kBlendOpAdd);
        device->SetRenderState(D3D8::kRsAlphaTestEnable, FALSE);
        device->SetRenderState(D3D8::kRsZEnable, FALSE);
        device->SetRenderState(D3D8::kRsZWriteEnable, FALSE);
        device->SetRenderState(D3D8::kRsFillMode, D3D8::kFillSolid);
        device->SetRenderState(D3D8::kRsCullMode, D3D8::kCullNone);
        device->SetRenderState(D3D8::kRsLighting, FALSE);
        device->SetRenderState(D3D8::kRsFogEnable, FALSE);
        device->SetRenderState(D3D8::kRsSpecularEnable, FALSE);
        device->SetRenderState(D3D8::kRsStencilEnable, FALSE);
        device->SetRenderState(D3D8::kRsWrap0, 0);
        device->SetRenderState(D3D8::kRsClipping, TRUE);
        device->SetRenderState(D3D8::kRsVertexBlend, 0);
        device->SetRenderState(D3D8::kRsIndexedVertexBlend, FALSE);
        device->SetRenderState(D3D8::kRsClipPlaneEnable, 0);
        device->SetRenderState(D3D8::kRsMultiSampleAntialias, FALSE);
        device->SetRenderState(D3D8::kRsColorWriteEnable, D3D8::kColorWriteAll);

        device->SetTextureStageState(0, D3D8::kTssColorOp, D3D8::kTopSelectArg1);
        device->SetTextureStageState(0, D3D8::kTssColorArg1, D3D8::kTaTexture);
        device->SetTextureStageState(0, D3D8::kTssAlphaOp, D3D8::kTopSelectArg1);
        device->SetTextureStageState(0, D3D8::kTssAlphaArg1, D3D8::kTaTexture);
        device->SetTextureStageState(0, D3D8::kTssTexCoordIndex, 0);
        device->SetTextureStageState(0, D3D8::kTssTextureTransformFlags, 0);
        device->SetTextureStageState(0, D3D8::kTssAddressU, D3D8::kTextureAddressClamp);
        device->SetTextureStageState(0, D3D8::kTssAddressV, D3D8::kTextureAddressClamp);
        // Texels map 1:1 to pixels, so point sampling keeps text crisp.
        device->SetTextureStageState(0, D3D8::kTssMagFilter, D3D8::kTextureFilterPoint);
        device->SetTextureStageState(0, D3D8::kTssMinFilter, D3D8::kTextureFilterPoint);
        device->SetTextureStageState(0, D3D8::kTssMipFilter, D3D8::kTextureFilterNone);
        device->SetTextureStageState(1, D3D8::kTssColorOp, D3D8::kTopDisable);
        device->SetTextureStageState(1, D3D8::kTssAlphaOp, D3D8::kTopDisable);

        device->DrawPrimitiveUP(D3D8::kTriangleStrip, 2, vertices, sizeof(Vertex));

        device->ApplyStateBlock(stateBlock);
        device->DeleteStateBlock(stateBlock);
    }
} // namespace Framework::GUI
