/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "view_d3d8.h"
#include "logging/logger.h"

#include "graphics/backend/d3d8.h"

#include <algorithm>
#include <cstdint>
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

    ViewD3D8::ViewD3D8(int id, Graphics::Renderer *graphicsRenderer, Manager *manager): View(id, graphicsRenderer, manager) {
    }

    ViewD3D8::~ViewD3D8() {
        ReleaseTexture();
    }

    Utils::Result<void, Framework::Error> ViewD3D8::Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated) {
        (void)gpuAccelerated;
        return View::Init(url, width, height, offsetX, offsetY, false);
    }

    void ViewD3D8::ReleaseTexture() {
        if (_texture) {
            _texture->Release();
            _texture = nullptr;
        }
        _textureWidth  = 0;
        _textureHeight = 0;
        _contentWidth  = 0;
        _contentHeight = 0;
    }

    bool ViewD3D8::UploadPixels(D3D8::Device *device) {
        auto *renderHandler = GetRenderHandler();
        if (!renderHandler) {
            return false;
        }

        // held for the whole read; OnPaint reallocates on resize
        const auto pixelLock = renderHandler->LockPixels();
        const auto &pixels   = renderHandler->GetPixelData();

        // resize in flight: keep the last frame until CEF repaints at the current size
        if (pixels.size() != static_cast<size_t>(_width) * static_cast<size_t>(_height) * 4) {
            return _texture != nullptr && _contentWidth == _width && _contentHeight == _height;
        }

        const int wantedWidth  = NextPowerOfTwo(_width);
        const int wantedHeight = NextPowerOfTwo(_height);
        bool fullUpload        = _contentWidth != _width || _contentHeight != _height;
        if (!_texture || _textureWidth != wantedWidth || _textureHeight != wantedHeight) {
            ReleaseTexture();
            // MANAGED survives the game's Reset after a lost device, so nothing is recreated then.
            if (FAILED(device->CreateTexture(wantedWidth, wantedHeight, 1, 0, D3D8::kFormatA8R8G8B8, D3D8::kPoolManaged, &_texture)) || !_texture) {
                _texture = nullptr;
                Framework::Logging::GetLogger("Web")->error("View {}: failed to create {}x{} D3D8 texture", _id, wantedWidth, wantedHeight);
                return false;
            }
            _textureWidth  = wantedWidth;
            _textureHeight = wantedHeight;
            fullUpload     = true;
        }

        if (!fullUpload && !renderHandler->IsPixelDataDirty()) {
            return true;
        }

        RECT region {0, 0, _width, _height};
        if (!fullUpload) {
            const CefRect &dirty = renderHandler->GetDirtyBounds();
            if (dirty.IsEmpty()) {
                renderHandler->ClearPixelDataDirty();
                return true;
            }
            region = {dirty.x, dirty.y, dirty.x + dirty.width, dirty.y + dirty.height};
        }

        D3D8::LockedRect locked {};
        if (FAILED(_texture->LockRect(0, &locked, &region, 0))) {
            // keep the dirty flag so the copy retries next frame
            return !fullUpload;
        }

        const size_t srcPitch = static_cast<size_t>(_width) * 4;
        const size_t rowBytes = static_cast<size_t>(region.right - region.left) * 4;
        const auto *src       = pixels.data() + static_cast<size_t>(region.top) * srcPitch + static_cast<size_t>(region.left) * 4;
        auto *dst             = static_cast<uint8_t *>(locked.bits);
        for (LONG y = region.top; y < region.bottom; ++y) {
            std::memcpy(dst, src, rowBytes);
            dst += locked.pitch;
            src += srcPitch;
        }
        _texture->UnlockRect(0);

        _contentWidth  = _width;
        _contentHeight = _height;
        renderHandler->ClearPixelDataDirty();
        return true;
    }

    void ViewD3D8::DrawQuad(D3D8::Device *device) const {
        // -0.5 aligns pixel centers to texel centers, as on Direct3D 9
        const float left   = static_cast<float>(_x) - 0.5f;
        const float top    = static_cast<float>(_y) - 0.5f;
        const float right  = left + static_cast<float>(_width);
        const float bottom = top + static_cast<float>(_height);
        const float u      = static_cast<float>(_width) / static_cast<float>(_textureWidth);
        const float v      = static_cast<float>(_height) / static_cast<float>(_textureHeight);
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

        const D3D8::Viewport viewport {0, 0, static_cast<DWORD>(_x + _width), static_cast<DWORD>(_y + _height), 0.0f, 1.0f};
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

    void ViewD3D8::Render() {
        if (!_browser || (!IsOnScreen() && !_offscreen)) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        auto *backend = _graphicsRenderer->GetD3D8Backend();
        if (!backend || !backend->IsDeviceReady()) {
            return;
        }
        auto *device = backend->GetDevice();

        if (!UploadPixels(device) || !_texture) {
            return;
        }

        if (!IsOnScreen()) {
            return; // offscreen: painted, not composited
        }

        DrawQuad(device);
    }
} // namespace Framework::GUI
