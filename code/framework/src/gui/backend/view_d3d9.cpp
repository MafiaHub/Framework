/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "view_d3d9.h"
#include "logging/logger.h"

#include "graphics/backend/d3d9.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Framework::GUI {
    namespace {
        struct Vertex {
            float x, y, z, rhw;
            D3DCOLOR color;
            float u, v;
        };
        constexpr DWORD kVertexFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    } // anonymous namespace

    ViewD3D9::ViewD3D9(int id, Graphics::Renderer *graphicsRenderer, Manager *manager): View(id, graphicsRenderer, manager) {
    }

    Utils::Result<void, Framework::Error> ViewD3D9::Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated) {
        (void)gpuAccelerated;
        return View::Init(url, width, height, offsetX, offsetY, false);
    }

    bool ViewD3D9::UploadPixels(IDirect3DDevice9 *device) {
        auto *renderHandler = GetRenderHandler();
        if (!renderHandler) {
            return false;
        }

        // held for the whole read; OnPaint reallocates on resize
        const auto pixelLock = renderHandler->LockPixels();
        const auto &pixels   = renderHandler->GetPixelData();

        // resize in flight: keep the last frame until CEF repaints at the current size
        if (pixels.size() != static_cast<size_t>(_width) * static_cast<size_t>(_height) * 4) {
            return _texture != nullptr;
        }

        const bool recreate = !_texture || _textureWidth != _width || _textureHeight != _height;
        if (recreate) {
            _texture.Reset();
            // MANAGED survives device resets, so no lost-device handling needed
            if (FAILED(device->CreateTexture(_width, _height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, _texture.ReleaseAndGetAddressOf(), nullptr))) {
                Framework::Logging::GetLogger("Web")->error("View {}: failed to create {}x{} D3D9 texture", _id, _width, _height);
                return false;
            }
            _textureWidth  = _width;
            _textureHeight = _height;
        }

        if (recreate || renderHandler->IsPixelDataDirty()) {
            D3DLOCKED_RECT locked;
            if (FAILED(_texture->LockRect(0, &locked, nullptr, 0))) {
                // keep the dirty flag so the copy retries next frame
                return true;
            }

            const size_t srcPitch = static_cast<size_t>(_width) * 4;
            const size_t rowBytes = std::min(srcPitch, static_cast<size_t>(locked.Pitch));
            const auto *src       = pixels.data();
            auto *dst             = static_cast<uint8_t *>(locked.pBits);
            for (int y = 0; y < _height; ++y) {
                std::memcpy(dst + static_cast<size_t>(y) * locked.Pitch, src + static_cast<size_t>(y) * srcPitch, rowBytes);
            }
            _texture->UnlockRect(0);
            renderHandler->ClearPixelDataDirty();
        }

        return true;
    }

    void ViewD3D9::DrawQuad(IDirect3DDevice9 *device) const {
        // -0.5 aligns pixel centers to texel centers on D3D9
        const float left     = static_cast<float>(_x) - 0.5f;
        const float top      = static_cast<float>(_y) - 0.5f;
        const float right    = left + static_cast<float>(_width);
        const float bottom   = top + static_cast<float>(_height);
        const D3DCOLOR white = D3DCOLOR_ARGB(255, 255, 255, 255);

        const Vertex vertices[4] = {
            {left, top, 0.0f, 1.0f, white, 0.0f, 0.0f},
            {right, top, 0.0f, 1.0f, white, 1.0f, 0.0f},
            {left, bottom, 0.0f, 1.0f, white, 0.0f, 1.0f},
            {right, bottom, 0.0f, 1.0f, white, 1.0f, 1.0f},
        };

        Microsoft::WRL::ComPtr<IDirect3DStateBlock9> stateBlock;
        if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &stateBlock))) {
            return;
        }

        device->SetVertexShader(nullptr);
        device->SetPixelShader(nullptr);
        device->SetFVF(kVertexFVF);
        device->SetTexture(0, _texture.Get());

        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
        device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);

        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

        device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(Vertex));

        stateBlock->Apply();
    }

    void ViewD3D9::Render() {
        if (!_browser || !_shouldDisplay) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        auto *backend = _graphicsRenderer->GetD3D9Backend();
        if (!backend) {
            return;
        }
        auto *device = backend->GetDevice();
        if (!device) {
            return;
        }

        if (!UploadPixels(device) || !_texture) {
            return;
        }

        DrawQuad(device);
    }

    void ViewD3D9::SubmitImGuiDraw() {
        if (!_browser || !_shouldDisplay) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        auto *backend = _graphicsRenderer->GetD3D9Backend();
        if (!backend) {
            return;
        }
        auto *device = backend->GetDevice();
        if (!device) {
            return;
        }

        if (!UploadPixels(device) || !_texture) {
            return;
        }

        // Composite into the background draw list so world-space overlays queued earlier
        // (nametags, labels) are occluded by opaque pages while ImGui windows stay on top.
        const ImVec2 pMin(static_cast<float>(_x), static_cast<float>(_y));
        const ImVec2 pMax(static_cast<float>(_x + _width), static_cast<float>(_y + _height));
        ImGui::GetBackgroundDrawList()->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(_texture.Get())), pMin, pMax);
    }
} // namespace Framework::GUI
