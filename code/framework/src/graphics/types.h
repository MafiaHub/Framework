/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <glm/mat4x4.hpp>
#include <vector>

namespace Framework::Graphics {
    enum class RendererBackend {
        BACKEND_D3D_9,
        BACKEND_D3D_11,
        BACKEND_D3D_12
    };

    enum class RendererAPI {
        IMGUI,
        CEF
    };

    enum class PlatformBackend {
        PLATFORM_WIN32,
    };

    enum class RendererState {
        STATE_NOT_INITIALIZED,
        STATE_READY,
        STATE_DEVICE_LOST,
        STATE_DEVICE_NOT_RESET,
        STATE_DRIVER_ERROR
    };

    enum class ShaderType : uint8_t {
        Fill,
        FillPath,
    };

    enum class VertexBufferFormat : uint8_t {
        _2f_4ub_2f,
        _2f_4ub_2f_2f_28f,
    };

    struct VertexBuffer {
        VertexBufferFormat format;
        uint32_t size;
        uint8_t *data;
    };

    using IndexType = uint32_t;

    struct IndexBuffer {
        uint32_t size;
        uint8_t *data;
    };

    enum class BitmapFormat : uint8_t {
        A8,
        BGRA8,
    };

    struct IntRect {
        int left, top, right, bottom;

        static constexpr IntRect MakeEmpty() {
            return IntRect {0, 0, 0, 0};
        }

        inline int width() const {
            return right - left;
        }
        inline int height() const {
            return bottom - top;
        }
        inline int x() const {
            return left;
        }
        inline int y() const {
            return top;
        }
        inline int center_x() const {
            return (int)std::round((left + right) * 0.5f);
        }
        inline int center_y() const {
            return (int)std::round((top + bottom) * 0.5f);
        }

        inline glm::vec2 origin() const {
            return glm::vec2 {(float)left, (float)top};
        }

        inline void SetEmpty() {
            *this = MakeEmpty();
        }

        inline bool IsEmpty() const {
            return *this == MakeEmpty();
        }

        inline bool IsValid() const {
            return width() > 0 && height() > 0;
        }

        inline void Inset(int dx, int dy) {
            left += dx;
            top += dy;
            right -= dx;
            bottom -= dy;
        }

        inline void Outset(int dx, int dy) {
            Inset(-dx, -dy);
        }

        inline void Move(int dx, int dy) {
            left += dx;
            top += dy;
            right += dx;
            bottom += dy;
        }

        inline int area() const {
            return width() * height();
        }

        inline void Join(const IntRect &rhs) {
            // if we are empty, just assign
            if (IsEmpty()) {
                *this = rhs;
            }
            else {
                if (rhs.left < left)
                    left = rhs.left;
                if (rhs.top < top)
                    top = rhs.top;
                if (rhs.right > right)
                    right = rhs.right;
                if (rhs.bottom > bottom)
                    bottom = rhs.bottom;
            }
        }

        inline void Join(const glm::vec2 &p) {
            // if we are empty, just assign
            if (IsEmpty()) {
                *this = {(int)std::floor(p.x), (int)std::floor(p.y), (int)std::ceil(p.x), (int)std::ceil(p.y)};
            }
            else {
                if ((int)std::floor(p.x) < left)
                    left = (int)std::floor(p.x);
                if ((int)std::floor(p.y) < top)
                    top = (int)std::floor(p.y);
                if ((int)std::ceil(p.x) > right)
                    right = (int)std::ceil(p.x);
                if ((int)std::ceil(p.y) > bottom)
                    bottom = (int)std::ceil(p.y);
            }
        }

        inline bool Contains(const glm::vec2 &p) const {
            return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
        }

        inline bool Contains(const IntRect &r) const {
            return left <= r.left && top <= r.top && right >= r.right && bottom >= r.bottom;
        }

        inline bool Intersects(const IntRect &rhs) const {
            // Since this is mostly used for pixel operations, we only count
            // intersections that have width and height >= 1.
            return !(rhs.left > right - 1 || rhs.right < left || rhs.top > bottom - 1 || rhs.bottom < top);
        }

        inline IntRect Intersect(const IntRect &other) const {
            return {(left < other.left) ? other.left : left, (top < other.top) ? other.top : top, (other.right < right) ? other.right : right, (other.bottom < bottom) ? other.bottom : bottom};
        }

        friend inline bool operator==(const IntRect &a, const IntRect &b) {
            return !memcmp(&a, &b, sizeof(a));
        }

        friend inline bool operator!=(const IntRect &a, const IntRect &b) {
            return !(a == b);
        }
    };

    struct Bitmap {
        BitmapFormat format;
        uint32_t width;
        uint32_t height;
        uint32_t pitch;
        uint32_t size;
        uint8_t *pixels;

        bool DrawBitmap(IntRect src_rect, IntRect dest_rect, Bitmap src, bool pad_repeat);
    };

    struct RenderBuffer {
        uint32_t texture_id;     // The backing texture for this RenderBuffer
        uint32_t width;          // The width of the RenderBuffer texture
        uint32_t height;         // The height of the RenderBuffer texture
        bool has_stencil_buffer; // Currently unused, always false.
        bool has_depth_buffer;   // Currently unsued, always false.
    };

    struct GPUState {
        /// Viewport width in pixels
        uint32_t viewport_width;

        /// Viewport height in pixels
        uint32_t viewport_height;

        /// Transform matrix-- you should multiply this with the screen-space orthographic projection
        /// matrix then pass to the vertex shader.
        glm::mat4 transform;

        /// Whether or not we should enable texturing for the current draw command.
        bool enable_texturing;

        /// Whether or not we should enable blending for the current draw command. If blending is
        /// disabled, any drawn pixels should overwrite existing. This is mainly used so we can modify
        /// alpha values of the RenderBuffer during scissored clears.
        bool enable_blend;

        /// The vertex/pixel shader program pair to use for the current draw command.
        ShaderType shader_type;

        /// The render buffer to use for the current draw command.
        uint32_t render_buffer_id;

        /// The texture id to bind to slot #1. (Will be 0 if none)
        uint32_t texture_1_id;

        /// The texture id to bind to slot #2. (Will be 0 if none)
        uint32_t texture_2_id;

        /// The texture id to bind to slot #3. (Will be 0 if none)
        uint32_t texture_3_id;

        /// The following four members are passed to the pixel shader via uniforms.
        float uniform_scalar[8];
        glm::vec4 uniform_vector[8];
        uint8_t clip_size;
        glm::mat4 clip[8];

        /// Whether or not scissor testing should be used for the current draw command.
        bool enable_scissor;

        /// The scissor rect to use for scissor testing (units in pixels)
        IntRect scissor_rect;
    };

    enum class CommandType : uint8_t {
        ClearRenderBuffer,
        DrawGeometry,
    };

    struct Command {
        CommandType command_type; // The type of command to dispatch.
        GPUState gpu_state;       // GPU state parameters for current command.

        /// The following members are only used with kCommandType_DrawGeometry
        uint32_t geometry_id;    // The geometry ID to bind
        uint32_t indices_count;  // The number of indices
        uint32_t indices_offset; // The index to start from
    };

    struct CommandList {
        uint32_t size;
        Command *commands;
    };
} // namespace Framework::Graphics
