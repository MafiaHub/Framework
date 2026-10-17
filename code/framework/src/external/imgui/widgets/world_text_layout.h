/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "world_text.h"
#include "world_text_markup.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace Framework::External::ImGUI::Widgets {
    enum class WorldTextAlign : uint8_t {
        Left,
        Center,
        Right,
    };

    // Which point of the text block sits on the projected anchor.
    enum class WorldTextAnchor : uint8_t {
        Center,
        Bottom,
        Top,
    };

    // A missing bold or italic face is synthesized from the nearest one that exists.
    struct WorldTextFonts {
        ImFont *regular    = nullptr; // null = the frame's current font
        ImFont *bold       = nullptr;
        ImFont *italic     = nullptr;
        ImFont *boldItalic = nullptr;
    };

    struct WorldTextSpan {
        ImFont *font = nullptr;
        std::optional<uint32_t> color; // 0xAARRGGBB; unset = the look's colour
        bool fauxBold   = false;
        bool fauxItalic = false;
    };

    struct WorldTextGlyph {
        ImWchar codepoint = 0;
        uint16_t span     = 0;
        float x           = 0.0f; // pen position from the block's top-left, in bakeSize pixels
        float y           = 0.0f;
        float advance     = 0.0f;
    };

    // Text laid out at one baked pixel size. It only holds code points and pen positions: glyph UVs
    // move whenever the atlas repacks, so they are looked up again on every draw.
    struct WorldTextLaidOut {
        float bakeSize = 0.0f;
        float width    = 0.0f;
        float height   = 0.0f;
        std::vector<WorldTextSpan> spans;
        std::vector<WorldTextGlyph> glyphs;
    };

    struct WorldTextLook {
        uint32_t color           = 0xFFFFFFFF; // all colours 0xAARRGGBB
        float outlineWidth       = 0.0f;
        uint32_t outlineColor    = 0xFF000000;
        float shadowX            = 0.0f;
        float shadowY            = 0.0f;
        float shadowBlur         = 0.0f;
        uint32_t shadowColor     = 0; // alpha 0 = no shadow
        uint32_t backgroundColor = 0; // alpha 0 = no plate
        float backgroundPadding  = 4.0f;
        float backgroundRadius   = 4.0f;
        WorldTextAnchor anchor   = WorldTextAnchor::Center;
    };

    constexpr float kWorldTextMaxOutline = 4.0f;
    constexpr float kWorldTextMaxBlur    = 8.0f;

    inline ImU32 WorldTextArgb(uint32_t argb) {
        return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
    }

    // Exact up to 24 px, then a ~8% ladder: a size that changes every frame (world-scaled text)
    // would otherwise rasterize a fresh copy of every glyph at each whole pixel it passes.
    inline float WorldTextBakeSize(float size) {
        constexpr float kExactUpTo = 24.0f;
        constexpr float kMaxBake   = 256.0f;
        if (!(size > 0.0f)) {
            return 0.0f;
        }
        if (size <= kExactUpTo) {
            return std::max(1.0f, std::round(size));
        }
        const float step = std::round(std::log(std::min(size, kMaxBake) / kExactUpTo) / std::log(1.08f));
        return std::min(kMaxBake, std::round(kExactUpTo * std::pow(1.08f, step)));
    }

    inline float WorldTextBoldOffset(float bakeSize) {
        return std::max(1.0f, bakeSize / 24.0f);
    }

    // wrapWidth and the result are in bakeSize pixels; wrapWidth <= 0 never wraps. Breaks after
    // spaces, and inside a word only when the word alone is wider than the line. ImGui thread only.
    inline WorldTextLaidOut LayoutWorldText(const std::vector<WorldTextRun> &runs, const WorldTextFonts &fonts, float bakeSize, float wrapWidth, float lineSpacing, WorldTextAlign align) {
        WorldTextLaidOut out;
        out.bakeSize = bakeSize;
        if (bakeSize <= 0.0f) {
            return out;
        }

        ImFont *regular    = fonts.regular ? fonts.regular : ImGui::GetFont();
        const auto spanFor = [&](const WorldTextRun &run) {
            WorldTextSpan span;
            span.color = run.color;
            span.font  = regular;
            if (run.bold && run.italic && fonts.boldItalic) {
                span.font = fonts.boldItalic;
            }
            else if (run.bold && fonts.bold) {
                span.font       = fonts.bold;
                span.fauxItalic = run.italic;
            }
            else if (run.italic && fonts.italic) {
                span.font     = fonts.italic;
                span.fauxBold = run.bold;
            }
            else {
                span.fauxBold   = run.bold;
                span.fauxItalic = run.italic;
            }
            return span;
        };

        struct Line {
            size_t begin;
            size_t end;
        };
        std::vector<Line> lines;
        const float lineHeight = bakeSize * std::max(0.1f, lineSpacing);
        const float boldExtra  = WorldTextBoldOffset(bakeSize);

        size_t lineBegin = 0;
        size_t breakAt   = 0; // first glyph after the line's last space; 0 = no break yet
        float penX       = 0.0f;
        float lineY      = 0.0f;

        const auto newLine = [&](size_t carryFrom) {
            lines.push_back({lineBegin, carryFrom});
            lineY += lineHeight;
            const float shift = carryFrom < out.glyphs.size() ? out.glyphs[carryFrom].x : penX; // nothing carried: the pen restarts
            for (size_t k = carryFrom; k < out.glyphs.size(); ++k) {
                out.glyphs[k].x -= shift;
                out.glyphs[k].y = lineY;
            }
            penX -= shift;
            lineBegin = carryFrom;
            breakAt   = 0;
        };

        for (const WorldTextRun &run : runs) {
            if (out.spans.size() >= UINT16_MAX) {
                break;
            }
            const auto spanIndex = static_cast<uint16_t>(out.spans.size());
            out.spans.push_back(spanFor(run));
            const WorldTextSpan &span = out.spans.back();
            ImFontBaked *baked        = span.font->GetFontBaked(bakeSize);
            if (!baked) {
                continue;
            }

            for (size_t i = 0; i < run.text.size();) {
                uint32_t cp = DecodeUtf8(run.text, i);
                if (cp == '\r') {
                    continue;
                }
                if (cp == '\n') {
                    newLine(out.glyphs.size());
                    continue;
                }
                if (cp == '\t') {
                    cp = ' ';
                }
                if (cp > IM_UNICODE_CODEPOINT_MAX) {
                    cp = 0xFFFD;
                }

                const auto codepoint = static_cast<ImWchar>(cp);
                const float advance  = baked->GetCharAdvance(codepoint) + (span.fauxBold ? boldExtra : 0.0f);
                if (wrapWidth > 0.0f && cp != ' ' && penX + advance > wrapWidth && out.glyphs.size() > lineBegin) {
                    newLine(breakAt > lineBegin ? breakAt : out.glyphs.size());
                }

                out.glyphs.push_back({codepoint, spanIndex, penX, lineY, advance});
                penX += advance;
                if (cp == ' ') {
                    breakAt = out.glyphs.size();
                }
            }
        }
        lines.push_back({lineBegin, out.glyphs.size()});

        // Trailing spaces never count toward a line's width, or alignment would leave a gap.
        std::vector<float> widths(lines.size(), 0.0f);
        for (size_t l = 0; l < lines.size(); ++l) {
            for (size_t k = lines[l].begin; k < lines[l].end; ++k) {
                if (out.glyphs[k].codepoint != ' ') {
                    widths[l] = std::max(widths[l], out.glyphs[k].x + out.glyphs[k].advance);
                }
            }
            out.width = std::max(out.width, widths[l]);
        }
        const float alignFactor = align == WorldTextAlign::Left ? 0.0f : (align == WorldTextAlign::Center ? 0.5f : 1.0f);
        for (size_t l = 0; l < lines.size(); ++l) {
            const float shift = std::round((out.width - widths[l]) * alignFactor);
            for (size_t k = lines[l].begin; k < lines[l].end; ++k) {
                out.glyphs[k].x += shift;
            }
        }
        out.height = static_cast<float>(lines.size() - 1) * lineHeight + bakeSize;
        return out;
    }

    // drawSize is the pixel size to draw at (the layout scales from its bakeSize); styleScale scales
    // outline, shadow and plate metrics alongside it. alpha multiplies every colour.
    inline void DrawWorldTextLaidOut(ImDrawList *drawList, const WorldTextLaidOut &text, const WorldTextLook &look, ImVec2 anchor, float drawSize, float styleScale, float alpha) {
        if (!drawList || text.glyphs.empty() || text.bakeSize <= 0.0f || drawSize <= 0.0f || alpha <= 0.0f) {
            return;
        }

        const float scale  = drawSize / text.bakeSize;
        const float width  = text.width * scale;
        const float height = text.height * scale;
        float originY      = anchor.y - height * 0.5f;
        if (look.anchor == WorldTextAnchor::Bottom) {
            originY = anchor.y - height;
        }
        else if (look.anchor == WorldTextAnchor::Top) {
            originY = anchor.y;
        }
        const ImVec2 origin(std::floor(anchor.x - width * 0.5f), std::floor(originY));

        if ((look.backgroundColor >> 24) != 0) {
            const float pad = look.backgroundPadding * styleScale;
            drawList->AddRectFilled(ImVec2(origin.x - pad, origin.y - pad), ImVec2(origin.x + width + pad, origin.y + height + pad), WorldTextModulateAlpha(WorldTextArgb(look.backgroundColor), alpha), look.backgroundRadius * styleScale);
        }

        constexpr float kItalicShear = 0.2f;
        const float boldOffset       = WorldTextBoldOffset(text.bakeSize) * scale;
        const ImU32 baseColor        = WorldTextModulateAlpha(WorldTextArgb(look.color), alpha);

        // Every glyph is looked up once, into quads relative to the origin; each outline and shadow
        // copy below only re-emits them. Glyph UVs move when the atlas repacks, so this is per draw.
        struct Quad {
            ImVec2 corners[4];
            ImVec2 uvMin;
            ImVec2 uvMax;
            ImU32 color;
            bool colored; // colour glyphs (emoji) have no silhouette for an outline or shadow
            ImFontAtlas *atlas;
        };
        static thread_local std::vector<Quad> quads;
        quads.clear();
        int spanIndex      = -1;
        ImFontBaked *baked = nullptr;
        ImU32 spanColor    = baseColor;
        for (const WorldTextGlyph &g : text.glyphs) {
            const WorldTextSpan &span = text.spans[g.span];
            if (g.span != spanIndex) {
                spanIndex = g.span;
                baked     = span.font->GetFontBaked(text.bakeSize);
                spanColor = span.color ? WorldTextModulateAlpha(WorldTextArgb(*span.color), alpha) : baseColor;
            }
            if (!baked) {
                continue;
            }
            const ImFontGlyph *glyph = baked->FindGlyph(g.codepoint);
            if (!glyph || !glyph->Visible) {
                continue;
            }
            const float x0 = (g.x + glyph->X0) * scale;
            const float x1 = (g.x + glyph->X1) * scale;
            const float y0 = (g.y + glyph->Y0) * scale;
            const float y1 = (g.y + glyph->Y1) * scale;
            float topShear = 0.0f, bottomShear = 0.0f;
            if (span.fauxItalic) {
                const float baseline = (g.y + baked->Ascent) * scale;
                topShear             = (baseline - y0) * kItalicShear;
                bottomShear          = (baseline - y1) * kItalicShear;
            }
            const ImU32 color = glyph->Colored ? (spanColor | ~IM_COL32_A_MASK) : spanColor;
            const int copies  = span.fauxBold ? 2 : 1;
            for (int c = 0; c < copies; ++c) {
                const float dx = c * boldOffset;
                quads.push_back(
                    {{ImVec2(x0 + topShear + dx, y0), ImVec2(x1 + topShear + dx, y0), ImVec2(x1 + bottomShear + dx, y1), ImVec2(x0 + bottomShear + dx, y1)}, ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V1), color, glyph->Colored != 0, span.font->OwnerAtlas});
            }
        }
        if (quads.empty()) {
            return;
        }

        // One pass of every quad. tint replaces the spans' colours (outline, shadow). Fonts from
        // different atlases draw from different textures, so each run of one atlas binds its own.
        ImFontAtlas *bound = nullptr;
        const auto pass    = [&](ImVec2 offset, std::optional<ImU32> tint) {
            if (tint && ((*tint >> IM_COL32_A_SHIFT) & 0xFF) == 0) {
                return;
            }
            const ImVec2 at(origin.x + offset.x, origin.y + offset.y);
            for (size_t begin = 0; begin < quads.size();) {
                ImFontAtlas *atlas = quads[begin].atlas;
                size_t end         = begin;
                int count          = 0;
                for (; end < quads.size() && quads[end].atlas == atlas; ++end) {
                    count += tint && quads[end].colored ? 0 : 1;
                }
                if (count > 0) {
                    if (atlas != bound) {
                        if (bound) {
                            drawList->PopTexture();
                        }
                        drawList->PushTexture(atlas->TexRef);
                        bound = atlas;
                    }
                    drawList->PrimReserve(6 * count, 4 * count);
                    for (size_t k = begin; k < end; ++k) {
                        const Quad &q = quads[k];
                        if (tint && q.colored) {
                            continue;
                        }
                        const ImVec2 *c = q.corners;
                        drawList->PrimQuadUV(ImVec2(at.x + c[0].x, at.y + c[0].y), ImVec2(at.x + c[1].x, at.y + c[1].y), ImVec2(at.x + c[2].x, at.y + c[2].y), ImVec2(at.x + c[3].x, at.y + c[3].y), q.uvMin, ImVec2(q.uvMax.x, q.uvMin.y), q.uvMax, ImVec2(q.uvMin.x, q.uvMax.y),
                            tint ? *tint : q.color);
                    }
                }
                begin = end;
            }
        };

        // A ring of offset copies: 8 directions for a hairline, more as the radius grows.
        const auto ring = [&](ImVec2 centre, float radius, ImU32 tint) {
            const int directions = radius <= 1.5f ? 8 : (radius <= 3.0f ? 12 : 16);
            for (int d = 0; d < directions; ++d) {
                const float angle = 6.2831853f * static_cast<float>(d) / static_cast<float>(directions);
                pass(ImVec2(centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius), tint);
            }
        };

        if ((look.shadowColor >> 24) != 0) {
            const ImVec2 at(look.shadowX * styleScale, look.shadowY * styleScale);
            const float blur = std::min(look.shadowBlur, kWorldTextMaxBlur) * styleScale;
            if (blur < 0.5f) {
                pass(at, WorldTextModulateAlpha(WorldTextArgb(look.shadowColor), alpha));
            }
            else {
                // Stacked faint copies stand in for a blur: ImGui has no filter pass.
                const ImU32 faint = WorldTextModulateAlpha(WorldTextArgb(look.shadowColor), alpha * 0.25f);
                pass(at, faint);
                ring(at, blur * 0.5f, faint);
                ring(at, blur, faint);
            }
        }

        const float outline = std::min(look.outlineWidth, kWorldTextMaxOutline) * styleScale;
        if (outline > 0.0f) {
            const ImU32 tint = WorldTextModulateAlpha(WorldTextArgb(look.outlineColor), alpha);
            if (outline > 2.0f) {
                ring(ImVec2(0.0f, 0.0f), outline * 0.5f, tint);
            }
            ring(ImVec2(0.0f, 0.0f), outline, tint);
        }

        pass(ImVec2(0.0f, 0.0f), std::nullopt);
        if (bound) {
            drawList->PopTexture();
        }
    }
} // namespace Framework::External::ImGUI::Widgets
