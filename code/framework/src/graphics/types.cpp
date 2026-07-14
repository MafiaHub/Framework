/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "types.h"

namespace Framework::Graphics {
    // Helper function to get pixel size in bytes
    int GetPixelSize(BitmapFormat fmt) {
        switch (fmt) {
        case BitmapFormat::A8: return 1;
        case BitmapFormat::BGRA8: return 4;
        default: return 0;
        }
    }

    // Helper function to convert between formats
    void ConvertAndCopyPixel(uint8_t *dest, BitmapFormat dest_fmt, uint8_t *src, BitmapFormat src_fmt) {
        // Handle A8 to A8 (direct copy)
        if (dest_fmt == BitmapFormat::A8 && src_fmt == BitmapFormat::A8) {
            *dest = *src;
            return;
        }

        // Handle BGRA8 to BGRA8 (direct copy)
        if (dest_fmt == BitmapFormat::BGRA8 && src_fmt == BitmapFormat::BGRA8) {
            *((uint32_t *)dest) = *((uint32_t *)src);
            return;
        }

        // Handle BGRA8 to A8 (take blue channel)
        if (dest_fmt == BitmapFormat::A8 && src_fmt == BitmapFormat::BGRA8) {
            *dest = src[0]; // Blue channel
            return;
        }

        // Handle A8 to BGRA8 (copy alpha value to all channels)
        if (dest_fmt == BitmapFormat::BGRA8 && src_fmt == BitmapFormat::A8) {
            dest[0] = dest[1] = dest[2] = *src; // B, G, R
            dest[3]                     = 255;  // A
            return;
        }
    }

    bool Bitmap::DrawBitmap(IntRect src_rect, IntRect dest_rect, Bitmap src, bool pad_repeat) {
        // Validate source rectangle
        if (src_rect.left < 0 || src_rect.top < 0 || src_rect.right > static_cast<int>(src.width) || src_rect.bottom > static_cast<int>(src.height)) {
            return false; // Source rectangle out of bounds
        }

        // Validate destination rectangle
        if (dest_rect.left < 0 || dest_rect.top < 0 || dest_rect.right > static_cast<int>(width) || dest_rect.bottom > static_cast<int>(height)) {
            return false; // Destination rectangle out of bounds
        }

        // Calculate dimensions
        int src_width   = src_rect.right - src_rect.left;
        int src_height  = src_rect.bottom - src_rect.top;
        int dest_width  = dest_rect.right - dest_rect.left;
        int dest_height = dest_rect.bottom - dest_rect.top;

        // If dimensions don't match and we're not padding, we need to scale
        bool scaling = (src_width != dest_width || src_height != dest_height) && !pad_repeat;

        // Handle different bitmap formats and draw
        for (int y = 0; y < dest_height; y++) {
            for (int x = 0; x < dest_width; x++) {
                // Calculate source coordinates with scaling if needed
                int src_x, src_y;

                if (scaling) {
                    src_x = src_rect.left + (x * src_width) / dest_width;
                    src_y = src_rect.top + (y * src_height) / dest_height;
                }
                else {
                    src_x = src_rect.left + x;
                    src_y = src_rect.top + y;
                }

                // Apply padding if requested
                if (pad_repeat) {
                    // Clamp to source boundaries
                    src_x = std::max(src_rect.left, std::min(src_x, src_rect.right - 1));
                    src_y = std::max(src_rect.top, std::min(src_y, src_rect.bottom - 1));
                }
                else if (src_x >= src_rect.right || src_y >= src_rect.bottom) {
                    // Skip if out of source bounds and not padding
                    continue;
                }

                // Calculate pixel pointers
                uint8_t *src_pixel  = src.pixels + (src_y * src.pitch) + GetPixelSize(src.format) * src_x;
                uint8_t *dest_pixel = pixels + ((dest_rect.top + y) * pitch) + GetPixelSize(format) * (dest_rect.left + x);

                // Convert and copy the pixel
                ConvertAndCopyPixel(dest_pixel, format, src_pixel, src.format);
            }
        }

        return true;
    }
} // namespace Framework::Graphics
