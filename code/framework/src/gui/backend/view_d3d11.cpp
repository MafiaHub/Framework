/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "view_d3d11.h"
#include "logging/logger.h"

#include "graphics/backend/d3d11.h"

namespace Framework::GUI {
    ViewD3D11::ViewD3D11(int id, Graphics::Renderer *graphicsRenderer, Manager *manager): View(id, graphicsRenderer, manager) {
    }

    Utils::Result<void, Framework::Error> ViewD3D11::Init(const std::string &url, int width, int height, int offsetX, int offsetY, bool gpuAccelerated) {
        return View::Init(url, width, height, offsetX, offsetY, gpuAccelerated);
    }

    void ViewD3D11::Update() {
        if (!_browser || !_shouldDisplay) {
            return;
        }

        std::scoped_lock lock(_renderMutex);
        View::Update();
    }

    void ViewD3D11::CreateOrUpdateGeometry() {
        auto *backend = _graphicsRenderer->GetD3D11Backend();
        if (!backend) {
            return;
        }

        // Build a simple full-screen quad using the complex vertex format
        // that the existing D3D11 backend shaders expect
        struct Vertex {
            float pos[2];
            uint8_t color[4];
            float tex[2];
            float obj[2];
            float data0[4];
            float data1[4];
            float data2[4];
            float data3[4];
            float data4[4];
            float data5[4];
            float data6[4];
        };

        float left   = static_cast<float>(_x);
        float top    = static_cast<float>(_y);
        float right  = static_cast<float>(_x + _width);
        float bottom = static_cast<float>(_y + _height);

        Vertex vertices[4] = {};

        // Fill type 1 = Image
        auto fillVertex = [](Vertex &v) {
            memset(&v, 0, sizeof(v));
            v.color[0] = 255;
            v.color[1] = 255;
            v.color[2] = 255;
            v.color[3] = 255;
            v.data0[0] = 1.0f; // Fill Type: Image
        };

        fillVertex(vertices[0]);
        vertices[0].pos[0] = vertices[0].obj[0] = left;
        vertices[0].pos[1] = vertices[0].obj[1] = top;
        vertices[0].tex[0] = 0.0f;
        vertices[0].tex[1] = 0.0f;

        fillVertex(vertices[1]);
        vertices[1].pos[0] = vertices[1].obj[0] = right;
        vertices[1].pos[1] = vertices[1].obj[1] = top;
        vertices[1].tex[0] = 1.0f;
        vertices[1].tex[1] = 0.0f;

        fillVertex(vertices[2]);
        vertices[2].pos[0] = vertices[2].obj[0] = right;
        vertices[2].pos[1] = vertices[2].obj[1] = bottom;
        vertices[2].tex[0] = 1.0f;
        vertices[2].tex[1] = 1.0f;

        fillVertex(vertices[3]);
        vertices[3].pos[0] = vertices[3].obj[0] = left;
        vertices[3].pos[1] = vertices[3].obj[1] = bottom;
        vertices[3].tex[0] = 0.0f;
        vertices[3].tex[1] = 1.0f;

        uint32_t indices[] = {0, 1, 3, 1, 2, 3};

        Graphics::VertexBuffer vb;
        vb.format = Graphics::VertexBufferFormat::_2f_4ub_2f_2f_28f;
        vb.size   = sizeof(vertices);
        vb.data   = reinterpret_cast<uint8_t *>(vertices);

        Graphics::IndexBuffer ib;
        ib.size = sizeof(indices);
        ib.data = reinterpret_cast<uint8_t *>(indices);

        if (!_geometryCreated) {
            _geometryID = backend->NextGeometryId();
            backend->CreateGeometry(_geometryID, vb, ib);
            _geometryCreated = true;
        }
        else {
            backend->UpdateGeometry(_geometryID, vb, ib);
        }
    }

    void ViewD3D11::Render() {
        if (!_browser || !_shouldDisplay) {
            return;
        }

        std::scoped_lock lock(_renderMutex);

        auto *backend = _graphicsRenderer->GetD3D11Backend();
        if (!backend) {
            return;
        }

        // Create/update geometry
        CreateOrUpdateGeometry();

        // Set up GPU state for rendering the quad
        Graphics::GPUState gpuState {};
        memset(&gpuState, 0, sizeof(gpuState));

        gpuState.viewport_width  = _width;
        gpuState.viewport_height = _height;
        gpuState.enable_scissor  = false;
        gpuState.enable_blend    = true;
        gpuState.enable_texturing = true;
        gpuState.shader_type      = Graphics::ShaderType::Fill;
        gpuState.render_buffer_id = 0;

        // Set identity transform
        gpuState.transform = glm::mat4(1.0f);

        if (_gpuAccelerated) {
            // GPU path: use shared texture from CEF
            auto *renderHandler = GetRenderHandler();
            if (renderHandler) {
                auto *sharedTex = renderHandler->GetSharedTexture();
                if (sharedTex) {
                    // Register the shared texture with the D3D11 backend if not already done
                    if (_textureID == 0) {
                        _textureID = backend->NextTextureId();
                    }

                    // Create a SRV from the shared texture and register with backend
                    auto &texEntry = backend->GetTexture(_textureID);
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
                    sharedTex->QueryInterface(IID_PPV_ARGS(&tex));
                    texEntry.texture = tex;

                    if (!texEntry.texture_srv) {
                        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc {};
                        srvDesc.Format                    = DXGI_FORMAT_B8G8R8A8_UNORM;
                        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
                        srvDesc.Texture2D.MipLevels       = 1;
                        srvDesc.Texture2D.MostDetailedMip = 0;
                        backend->GetDevice()->CreateShaderResourceView(sharedTex, &srvDesc, &texEntry.texture_srv);
                    }

                    gpuState.texture_1_id = _textureID;
                }
            }
        }
        else {
            // CPU path: use pixel data from CEF's OnPaint
            auto *renderHandler = GetRenderHandler();
            if (renderHandler) {
                // held for the whole read; OnPaint reallocates on resize
                const auto pixelLock = renderHandler->LockPixels();

                if (!renderHandler->GetPixelData().empty()) {
                    Graphics::Bitmap bmp;
                    bmp.format = Graphics::BitmapFormat::BGRA8;
                    bmp.width  = _width;
                    bmp.height = _height;
                    bmp.pitch  = _width * 4;
                    bmp.size   = static_cast<uint32_t>(renderHandler->GetPixelData().size());
                    bmp.pixels = const_cast<uint8_t *>(renderHandler->GetPixelData().data());

                    if (_cpuTextureID == 0) {
                        _cpuTextureID = backend->NextTextureId();
                        backend->CreateTexture(_cpuTextureID, bmp);
                    }
                    else if (renderHandler->IsPixelDataDirty()) {
                        backend->UpdateTexture(_cpuTextureID, bmp);
                        renderHandler->ClearPixelDataDirty();
                    }

                    gpuState.texture_1_id = _cpuTextureID;
                }
            }
        }

        // Draw the geometry
        if (gpuState.texture_1_id != 0) {
            backend->DrawGeometry(_geometryID, 6, 0, gpuState);
        }
    }
} // namespace Framework::GUI
