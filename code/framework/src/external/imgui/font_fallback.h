/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <imgui.h>

#include <cfloat>

namespace Framework::External::ImGUI {
    // Merges the fallback's sources into the face behind its own, so a glyph the face lacks draws in the
    // fallback rather than as '?'. They share the fallback's data, and draw at the face's scale.
    inline void MergeFontFallback(ImFontAtlas &atlas, ImFont *face, const ImFont *fallback) {
        constexpr int kMaxSources = 16; // ImFontGlyph::SourceIdx is 4 bits
        if (!face || !fallback || face == fallback) {
            return;
        }
        for (int i = 0; i < fallback->Sources.Size && face->Sources.Size < kMaxSources; ++i) {
            // A copy each time: adding a source moves the atlas's array the pointers index.
            ImFontConfig cfg         = *fallback->Sources[i];
            cfg.MergeMode            = true;
            cfg.DstFont              = face;
            cfg.FontDataOwnedByAtlas = false; // the fallback's own source frees it
            cfg.FontLoaderData       = nullptr;
            cfg.EllipsisChar         = 0; // the face's own, found first
            // Offsets and advances are tuned to the fallback's reference size, not the face's.
            cfg.SizePixels         = 0.0f;
            cfg.GlyphOffset        = ImVec2(0.0f, 0.0f);
            cfg.GlyphMinAdvanceX   = 0.0f;
            cfg.GlyphMaxAdvanceX   = FLT_MAX;
            cfg.GlyphExtraAdvanceX = 0.0f;
            atlas.AddFont(&cfg);
        }
    }
} // namespace Framework::External::ImGUI
