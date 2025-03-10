#include "renderer_d3d11.h"

#include <Ultralight/platform/Platform.h>

#include "graphics/backend/d3d11.h"

namespace Framework::GUI {
    bool RendererD3D11::Init(Graphics::Renderer *renderer) {
        if (renderer->GetBackendType() != Graphics::RendererBackend::BACKEND_D3D_11) {
            return false;
        }

        _renderer = renderer->GetD3D11Backend();
        return true;
    }

    void RendererD3D11::BeginDrawing() {
        _renderer->BeginDrawing();
    }
    void RendererD3D11::EndDrawing() {
        _renderer->EndDrawing();
    }
    void RendererD3D11::BindTexture(uint8_t texture_unit, uint32_t texture_id) {
        _renderer->BindTexture(texture_unit, texture_id);
    }
    void RendererD3D11::BindRenderBuffer(uint32_t render_buffer_id) {
        _renderer->BindRenderBuffer(render_buffer_id);
    }
    void RendererD3D11::ClearRenderBuffer(uint32_t render_buffer_id) {
        _renderer->ClearRenderBuffer(render_buffer_id);
    }
    void RendererD3D11::DrawGeometry(uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const ultralight::GPUState &state) {
        _renderer->DrawGeometry(geometry_id, indices_count, indices_offset, Convert(state));
    }
    bool RendererD3D11::HasCommandsPending() {
        return _renderer->HasCommandsPending();
    }
    void RendererD3D11::DrawCommandList() {
        _renderer->DrawCommandList();
    }
    void RendererD3D11::BeginSynchronize() {
        _renderer->BeginSynchronize();
    }
    void RendererD3D11::EndSynchronize() {
        _renderer->EndSynchronize();
    }
    uint32_t RendererD3D11::NextTextureId() {
        return _renderer->NextTextureId();
    }
    uint32_t RendererD3D11::NextRenderBufferId() {
        return _renderer->NextRenderBufferId();
    }
    uint32_t RendererD3D11::NextGeometryId() {
        return _renderer->NextGeometryId();
    }
    void RendererD3D11::UpdateCommandList(const ultralight::CommandList &list) {
        static Framework::Graphics::CommandList newList {};
        if (newList.size < list.size) {
            delete[] newList.commands;
            newList.commands = new Framework::Graphics::Command[list.size];
        }
        newList.size = list.size;
        for (int i = 0; i < list.size; ++i) {
            newList.commands[i] = Convert(list.commands[i]);
        }
        _renderer->UpdateCommandList(newList);
    }

    void RendererD3D11::CreateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) {
        Framework::Graphics::Bitmap bmp = Convert(bitmap);
        _renderer->CreateTexture(texture_id, bmp);
        UnlockBitmap(bitmap);
    }

    void RendererD3D11::UpdateTexture(uint32_t texture_id, ultralight::RefPtr<ultralight::Bitmap> bitmap) {
        Framework::Graphics::Bitmap bmp = Convert(bitmap);
        _renderer->UpdateTexture(texture_id, bmp);
        UnlockBitmap(bitmap);
    }

    void RendererD3D11::DestroyTexture(uint32_t texture_id) {
        _renderer->DestroyTexture(texture_id);
    }

    void RendererD3D11::CreateRenderBuffer(uint32_t render_buffer_id, const ultralight::RenderBuffer &buffer) {
        _renderer->CreateRenderBuffer(render_buffer_id, Convert(buffer));
    }

    void RendererD3D11::DestroyRenderBuffer(uint32_t render_buffer_id) {
        _renderer->DestroyRenderBuffer(render_buffer_id);
    }

    void RendererD3D11::CreateGeometry(uint32_t geometry_id, const ultralight::VertexBuffer &vertices, const ultralight::IndexBuffer &indices) {
        _renderer->CreateGeometry(geometry_id, Convert(vertices), Convert(indices));
    }

    void RendererD3D11::UpdateGeometry(uint32_t geometry_id, const ultralight::VertexBuffer &vertices, const ultralight::IndexBuffer &indices) {
        _renderer->UpdateGeometry(geometry_id, Convert(vertices), Convert(indices));
    }

    void RendererD3D11::DestroyGeometry(uint32_t geometry_id) {
        _renderer->DestroyGeometry(geometry_id);
    }

    Framework::Graphics::Command RendererD3D11::Convert(ultralight::Command cmd) {
        Framework::Graphics::Command newCmd;
        newCmd.command_type = cmd.command_type == ultralight::CommandType::ClearRenderBuffer ? Framework::Graphics::CommandType::ClearRenderBuffer : Framework::Graphics::CommandType::DrawGeometry;
        newCmd.geometry_id  = cmd.geometry_id;
        newCmd.gpu_state    = Convert(cmd.gpu_state);
        newCmd.indices_count = cmd.indices_count;
        newCmd.indices_offset = cmd.indices_offset;

        return newCmd;
    }

    Framework::Graphics::GPUState RendererD3D11::Convert(ultralight::GPUState state) {
        Framework::Graphics::GPUState newState;

        newState.viewport_width  = state.viewport_width;
        newState.viewport_height = state.viewport_height;
        newState.transform = Convert(state.transform);
        newState.enable_texturing = state.enable_texturing;
        newState.enable_blend = state.enable_blend;
        newState.shader_type      = state.shader_type == ultralight::ShaderType::Fill ? Framework::Graphics::ShaderType::Fill : Framework::Graphics::ShaderType::FillPath;
        newState.render_buffer_id = state.render_buffer_id;
        newState.texture_1_id     = state.texture_1_id;
        newState.texture_2_id     = state.texture_2_id;
        newState.texture_3_id     = state.texture_3_id;
        newState.clip_size        = state.clip_size;
        for (int i = 0; i < 8; i++) {
            newState.clip[i] = Convert(state.clip[i]);
            newState.uniform_scalar[i] = state.uniform_scalar[i];
            newState.uniform_vector[i] = Convert(state.uniform_vector[i]);
        }
        newState.scissor_rect = Convert(state.scissor_rect);
        newState.enable_scissor = state.enable_scissor;

        return newState;
    }
    Framework::Graphics::IntRect RendererD3D11::Convert(ultralight::IntRect rect) {
        Framework::Graphics::IntRect newRect;
        newRect.left = rect.left;
        newRect.right = rect.right;
        newRect.top = rect.top;
        newRect.bottom = rect.bottom;
        return newRect;
    }
    Framework::Graphics::Bitmap RendererD3D11::Convert(ultralight::RefPtr<ultralight::Bitmap> bitmap) {
        Framework::Graphics::Bitmap newBitmap;
        newBitmap.width = bitmap->width();
        newBitmap.height = bitmap->height();
        newBitmap.pitch = bitmap->row_bytes();
        newBitmap.size = bitmap->size();
        newBitmap.pixels = (uint8_t *)bitmap->LockPixels();
        newBitmap.format = bitmap->format() == ultralight::BitmapFormat::A8_UNORM ? Framework::Graphics::BitmapFormat::A8 : Framework::Graphics::BitmapFormat::BGRA8;
        return newBitmap;
    }
    Framework::Graphics::RenderBuffer RendererD3D11::Convert(ultralight::RenderBuffer renderBuffer) {
        Framework::Graphics::RenderBuffer newRenderBuffer;
        newRenderBuffer.has_depth_buffer = renderBuffer.has_depth_buffer;
        newRenderBuffer.has_stencil_buffer = renderBuffer.has_stencil_buffer;
        newRenderBuffer.width              = renderBuffer.width;
        newRenderBuffer.height             = renderBuffer.height;
        newRenderBuffer.texture_id         = renderBuffer.texture_id;
        return newRenderBuffer;
    }
    Framework::Graphics::VertexBuffer RendererD3D11::Convert(ultralight::VertexBuffer buffer) {
        Framework::Graphics::VertexBuffer newBuffer;
        newBuffer.data = buffer.data;
        newBuffer.size = buffer.size;
        newBuffer.format = buffer.format == ultralight::VertexBufferFormat::_2f_4ub_2f ? Framework::Graphics::VertexBufferFormat::_2f_4ub_2f : Framework::Graphics::VertexBufferFormat::_2f_4ub_2f_2f_28f;
        return newBuffer;
    }
    Framework::Graphics::IndexBuffer RendererD3D11::Convert(ultralight::IndexBuffer buffer) {
        Framework::Graphics::IndexBuffer newBuffer;
        newBuffer.data = buffer.data;
        newBuffer.size = buffer.size;
        return newBuffer;
    }
    void RendererD3D11::UnlockBitmap(ultralight::RefPtr<ultralight::Bitmap> bitmap) {
        bitmap->UnlockPixels();
    }
    glm::vec4 RendererD3D11::Convert(ultralight::vec4 vec) {
        return glm::vec4(vec.x, vec.y, vec.z, vec.w);
    }
    glm::mat4 RendererD3D11::Convert(ultralight::Matrix4x4 mat) {
        return glm::mat4(
            mat.data[0], mat.data[4], mat.data[8], mat.data[12],
            mat.data[1], mat.data[5], mat.data[9], mat.data[13],
            mat.data[2], mat.data[6], mat.data[10], mat.data[14],
            mat.data[3], mat.data[7], mat.data[11], mat.data[15]
        );
    }
} // namespace Framework::GUI
