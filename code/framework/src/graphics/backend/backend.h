/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "graphics/types.h"
#include "graphics/renderer.h"

namespace Framework::Graphics {
    template <typename TDevice, typename TContext, typename TSwapChain, typename TCommandQueue>
    class Backend {
      protected:
        TDevice _device {};
        TContext _context {};
        uint32_t _nextTextureID       = 1;
        uint32_t _nextRenderBufferID = 1; // render buffer id 0 is reserved for default render target view.
        uint32_t _nextGeometryID      = 1;
        std::vector<Command> _commandList;
        int _batchCount = 0;

      public:
        Backend() = default;

        [[nodiscard]] virtual bool Init(const Framework::Graphics::RendererConfiguration &opts) = 0;
        virtual void Shutdown()                                                   = 0;

        virtual void Update() = 0;

        virtual void Render() {
            if (HasCommandsPending()) {
                DrawCommandList();
            }
        }

        virtual void Paint() {}

        bool IsInitialized() const {
            return _device != nullptr;
        }

        TDevice GetDevice() const {
            return _device;
        }

        void SetDevice(TDevice device) {
            _device = device;
        }

        TContext GetContext() const {
            return _context;
        }

        void SetContext(TContext ctx) {
            _context = ctx;
        }

        // implemented by GPU backend renderers
        virtual void BeginDrawing() = 0;
        virtual void EndDrawing() = 0;
        virtual void BindTexture(uint8_t texture_unit, uint32_t texture_id) = 0;
        virtual void BindRenderBuffer(uint32_t render_buffer_id) = 0;
        virtual void ClearRenderBuffer(uint32_t render_buffer_id) = 0;
        virtual void DrawGeometry(uint32_t geometry_id, uint32_t indices_count, uint32_t indices_offset, const GPUState &state) = 0;
        virtual void CreateTexture(uint32_t texture_id, Bitmap bitmap)                                                          = 0;
        virtual void UpdateTexture(uint32_t texture_id, Bitmap bitmap)                                                          = 0;
        virtual void DestroyTexture(uint32_t texture_id)                                                                        = 0;
        virtual void CreateRenderBuffer(uint32_t render_buffer_id, const RenderBuffer &buffer)                                  = 0;
        virtual void DestroyRenderBuffer(uint32_t render_buffer_id)                                                             = 0;
        virtual void CreateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices)             = 0;
        virtual void UpdateGeometry(uint32_t geometry_id, const VertexBuffer &vertices, const IndexBuffer &indices)             = 0;
        virtual void DestroyGeometry(uint32_t geometry_id)                                                                      = 0;
        virtual void SetViewport(uint32_t width, uint32_t height)                                                               = 0;
        virtual glm::mat4 ApplyProjection(const glm::mat4 &transform, float screen_width, float screen_height)                  = 0;

        virtual void BeginSynchronize() {};
        virtual void EndSynchronize() {};

        virtual bool HasCommandsPending() {
            return !_commandList.empty();
        }

        virtual void DrawCommandList() {
            if (_commandList.empty())
                return;

            _batchCount = 0;

            for (auto &cmd : _commandList) {
                if (cmd.command_type == CommandType::DrawGeometry)
                    DrawGeometry(cmd.geometry_id, cmd.indices_count, cmd.indices_offset, cmd.gpu_state);
                else if (cmd.command_type == CommandType::ClearRenderBuffer)
                    ClearRenderBuffer(cmd.gpu_state.render_buffer_id);
                _batchCount++;
            }

            _commandList.clear();
        }

        virtual uint32_t NextTextureId() {
            return _nextTextureID++;
        }
        virtual uint32_t NextRenderBufferId() {
            return _nextRenderBufferID++;
        }
        virtual uint32_t NextGeometryId() {
            return _nextGeometryID++;
        }
        virtual int GetBatchCount() {
            return _batchCount;
        }

        virtual void UpdateCommandList(const CommandList& list) {
            if (list.size) {
                _commandList.resize(list.size);
                std::memcpy(&_commandList[0], list.commands, sizeof(Command) * list.size);
            }
        }
    };
} // namespace Framework::Graphics
