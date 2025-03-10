#include "view_d3d11.h"
#include "logging/logger.h"

// DirectX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>

#include <unordered_map>

#include "gui/backend/renderer_d3d11.h"

static ultralight::IndexType patternCW[]  = {0, 1, 3, 1, 2, 3};
static ultralight::IndexType patternCCW[] = {0, 3, 1, 1, 3, 2};

struct ID3D11CommandList;

Framework::GUI::RendererD3D11 *rendererBackend {};
ID3D11CommandList *commandList {};

namespace Framework::GUI {
    ViewD3D11::ViewD3D11(ultralight::RefPtr<ultralight::Renderer> renderer, Graphics::Renderer *graphicsRenderer): View(renderer, graphicsRenderer) {
        _sdk = new SDK;
    }

    bool ViewD3D11::Init(std::string &path, int width, int height, bool gpu_accelerated) {
        return View::Init(path, width, height, gpu_accelerated);
    }

    void ViewD3D11::UpdateGeometry() {
        const auto driver = rendererBackend;
        
        bool is_new = false;

        float uv_l = 0.0f;
        float uv_t = 0.0f;
        float uv_r = 1.0f;
        float uv_b = 1.0f;

        if (_vertices.empty()) {
            _vertices.resize(4);
            _indices.resize(6);

            auto &config = ultralight::Platform::instance().config();

            if (config.face_winding == ultralight::FaceWinding::Clockwise) {
                std::memcpy(_indices.data(), patternCW, sizeof(ultralight::IndexType) * _indices.size());
            }
            else {
                std::memcpy(_indices.data(), patternCCW, sizeof(ultralight::IndexType) * _indices.size());
            }

            std::memset(&_gpuState, 0, sizeof(_gpuState));
            ultralight::Matrix identity;
            identity.SetIdentity();

            _gpuState.viewport_width   = _width;
            _gpuState.viewport_height  = _height;
            _gpuState.transform        = identity.GetMatrix4x4();
            _gpuState.enable_scissor   = false;
            _gpuState.enable_blend     = true;
            _gpuState.enable_texturing = true;
            _gpuState.shader_type      = ultralight::ShaderType::Fill;
            _gpuState.render_buffer_id = 0;
            
            if (_gpuAccelerated) {
                ultralight::RenderTarget target = _internalView->render_target();
                _gpuState.texture_1_id          = target.texture_id;

                uv_l = target.uv_coords.left;
                uv_t = target.uv_coords.top;
                uv_r = target.uv_coords.right;
                uv_b = target.uv_coords.bottom;
            }

            is_new = true;
        }

        if (!_needsUpdate) {
            return;
        }

        ultralight::Vertex_2f_4ub_2f_2f_28f v;
        memset(&v, 0, sizeof(v));

        v.data0[0] = 1; // Fill Type: Image

        v.color[0] = 255;
        v.color[1] = 255;
        v.color[2] = 255;
        v.color[3] = 255;

        auto x_      = 0;
        auto y_      = 0;
        float left   = static_cast<float>(x_);
        float top    = static_cast<float>(y_);
        float right  = static_cast<float>(x_ + _width);
        float bottom = static_cast<float>(y_ + _height);

        // TOP LEFT
        v.pos[0] = v.obj[0] = left;
        v.pos[1] = v.obj[1] = top;
        v.tex[0]            = uv_l;
        v.tex[1]            = uv_t;

        _vertices[0] = v;

        // TOP RIGHT
        v.pos[0] = v.obj[0] = right;
        v.pos[1] = v.obj[1] = top;
        v.tex[0]            = uv_r;
        v.tex[1]            = uv_t;

        _vertices[1] = v;

        // BOTTOM RIGHT
        v.pos[0] = v.obj[0] = right;
        v.pos[1] = v.obj[1] = bottom;
        v.tex[0]            = uv_r;
        v.tex[1]            = uv_b;

        _vertices[2] = v;

        // BOTTOM LEFT
        v.pos[0] = v.obj[0] = left;
        v.pos[1] = v.obj[1] = bottom;
        v.tex[0]            = uv_l;
        v.tex[1]            = uv_b;

        _vertices[3] = v;

        ultralight::VertexBuffer vbuffer;
        vbuffer.format = ultralight::VertexBufferFormat::_2f_4ub_2f_2f_28f;
        vbuffer.size   = static_cast<uint32_t>(sizeof(ultralight::Vertex_2f_4ub_2f_2f_28f) * _vertices.size());
        vbuffer.data   = (uint8_t *)_vertices.data();

        ultralight::IndexBuffer ibuffer;
        ibuffer.size = static_cast<uint32_t>(sizeof(ultralight::IndexType) * _indices.size());
        ibuffer.data = (uint8_t *)_indices.data();

        if (is_new) {
            _geometryID = driver->NextGeometryId();
            driver->CreateGeometry(_geometryID, vbuffer, ibuffer);
        }
        else {
            driver->UpdateGeometry(_geometryID, vbuffer, ibuffer);
        }

        _needsUpdate = false;
    }

    void ViewD3D11::Update() {
        if (!_internalView || !_shouldDisplay) {
            return;
        }

        std::lock_guard lock(_renderMutex);

        // Update the view content
        View::Update();
    }

    void ViewD3D11::Render() {
        if (!_internalView || !_shouldDisplay) {
            return;
        }

        std::lock_guard lock(_renderMutex);

        // Update D3D11 internal resources
        UpdateGeometry();

        // Update texture data for CPU renderer
        if (!_gpuAccelerated && _internalView->surface()) {
            const auto driver = rendererBackend;
            auto bitmap       = ((ultralight::BitmapSurface *)_internalView->surface())->bitmap();
            
            if (!_renderTextureID) {
                _renderTextureID = rendererBackend->NextTextureId();
                rendererBackend->CreateTexture(_renderTextureID, bitmap);
            }
            else {
                rendererBackend->UpdateTexture(_renderTextureID, bitmap);
            }

            _gpuState.texture_1_id = _renderTextureID;
        }

        // Issue a view render
        rendererBackend->DrawGeometry(_geometryID, 6, 0, _gpuState);
    }

    void ViewD3D11::InitRenderer(Framework::Graphics::Renderer *graphicsRenderer) {
        if (!rendererBackend) {
            rendererBackend = new RendererD3D11();
            rendererBackend->Init(graphicsRenderer);

            ultralight::Platform::instance().set_gpu_driver(rendererBackend);
        }
    }

    void ViewD3D11::UpdateRenderer() {
        if (!rendererBackend) {
            return;
        }

        if (rendererBackend->HasCommandsPending()) {
            rendererBackend->DrawCommandList();
        }

        // TODO figure out rendering done on a different thread than a render thread
        /*ID3D11CommandList *cc;
        rendererBackend->GetBackend()->GetDeferredContext()->FinishCommandList(false, &cc);

        if (commandList)
            commandList->Release();
        commandList = cc;

        ID3D11DeviceContext *ctx = rendererBackend->GetBackend()->GetImmediateContext();
        if (cc && ctx) {
            ctx->ExecuteCommandList(cc, true);
        }*/
    }
} // namespace Framework::GUI
