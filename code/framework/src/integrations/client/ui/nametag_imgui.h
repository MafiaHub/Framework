/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "nametag_list.h"

#include <external/imgui/widgets/nametag.h>
#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace Framework::Integrations::Client::UI::Nametags {
    // The ImGui backend for a resolved nametag list, for games with no retained 2D layer of their own
    // to pool nodes in. Stateless: a Resolved carries everything a tag needs, so there is no pool to
    // keep. The widget grows upward from the anchor -- note, name, health bar -- so a note appearing
    // never moves the name.

    // Green at full, yellow at half, red at none.
    inline ImU32 NametagHealthColor(float healthPercent, float alpha) {
        const float t   = std::clamp(healthPercent / 100.0f, 0.0f, 1.0f);
        const int red   = t > 0.5f ? static_cast<int>(255.0f * (1.0f - t) * 2.0f) : 255;
        const int green = t > 0.5f ? 255 : static_cast<int>(255.0f * t * 2.0f);
        return IM_COL32(red, green, 0, static_cast<int>(255.0f * std::clamp(alpha, 0.0f, 1.0f)));
    }

    // 0xAARRGGBB (the wire order the whole nametag path uses) -> ImU32, with `alpha` multiplied in.
    inline ImU32 NametagColor(uint32_t argb, float alpha) {
        const uint32_t faded = ModulateAlpha(argb, alpha);
        return IM_COL32((faded >> 16) & 0xFF, (faded >> 8) & 0xFF, faded & 0xFF, (faded >> 24) & 0xFF);
    }

    namespace Detail {
        // One shadowed, centred line. `end` may be null for a NUL-terminated line.
        inline void DrawShadowedLine(ImDrawList *drawList, ImFont *font, float fontPx, float centerX, float top, const char *line, const char *end, ImU32 color, ImU32 shadow, float shadowPx) {
            const float width = font->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, line, end).x;
            const ImVec2 pos(centerX - width * 0.5f, top);
            drawList->AddText(font, fontPx, ImVec2(pos.x + shadowPx, pos.y + shadowPx), shadow, line, end);
            drawList->AddText(font, fontPx, pos, color, line, end);
        }

        inline int CountLines(const char *text) {
            int lines = 1;
            for (const char *at = text; *at; ++at) {
                if (*at == '\n') {
                    ++lines;
                }
            }
            return lines;
        }
    } // namespace Detail

    // `screen` is the anchor in pixels, `viewport` the display size the Appearance fractions resolve
    // against, and `alpha` the mod's own multiplier (occlusion fade) on top of tag.distanceAlpha.
    // `font` null draws with the current font; a mod's own comes from Wrapper::GetFont.
    inline void DrawResolvedNametag(ImDrawList *drawList, const Resolved &tag, ImVec2 screen, float alpha, const Appearance &appearance, ImVec2 viewport, ImFont *font = nullptr) {
        alpha *= tag.distanceAlpha;
        if (!drawList || alpha <= 0.0f || viewport.x <= 0.0f || viewport.y <= 0.0f) {
            return;
        }

        font                  = font ? font : ImGui::GetFont();
        const float namePx    = appearance.fontHeight * viewport.y * tag.scale;
        const float notePx    = appearance.noteFontHeight * viewport.y * tag.scale;
        const float noteGap   = appearance.noteGap * viewport.y * tag.scale;
        const float shadowPx  = appearance.shadowOffset * viewport.y * tag.scale;
        const float barWidth  = appearance.healthBarWidth * viewport.x * tag.scale;
        const float barHeight = appearance.healthBarHeight * viewport.y * tag.scale;
        const float barGap    = appearance.healthBarGap * viewport.y * tag.scale;
        const float barBorder = appearance.healthBarBorder * viewport.y * tag.scale;

        const bool drawName   = tag.label && tag.label[0];
        const bool drawNote   = tag.note && tag.note[0];
        const bool drawHealth = tag.healthPercent >= 0.0f && barWidth > 0.0f && barHeight > 0.0f;

        // The lift is a fraction of screen height, so it reads the same at any resolution.
        screen.y -= tag.shift * viewport.y;

        const float noteHeight = drawNote ? static_cast<float>(Detail::CountLines(tag.note)) * notePx + noteGap : 0.0f;
        const float nameHeight = drawName ? namePx : 0.0f;
        const float barBlock   = drawHealth ? barGap + barHeight + barBorder * 2.0f : 0.0f;
        float top              = screen.y - (noteHeight + nameHeight + barBlock);

        const ImU32 shadow = NametagColor(appearance.shadowColor, alpha);

        if (drawNote) {
            const ImU32 color = NametagColor(tag.noteColor != 0 ? tag.noteColor : tag.color, alpha);
            for (const char *line = tag.note; line;) {
                const char *end = std::strchr(line, '\n');
                Detail::DrawShadowedLine(drawList, font, notePx, screen.x, top, line, end, color, shadow, shadowPx);
                top += notePx;
                line = end ? end + 1 : nullptr;
            }
            top += noteGap;
        }

        if (drawName) {
            Detail::DrawShadowedLine(drawList, font, namePx, screen.x, top, tag.label, nullptr, NametagColor(tag.color, alpha), shadow, shadowPx);

            // Off the left of the centred name, so talking never moves the name itself.
            if (tag.voiceLevel >= 0.0f) {
                const float nameWidth = font->CalcTextSizeA(namePx, FLT_MAX, 0.0f, tag.label).x;
                const ImVec2 center(screen.x - nameWidth * 0.5f - namePx * 0.7f, top + namePx * 0.5f);
                External::ImGUI::Widgets::DrawVoiceIcon(drawList, ImVec2(center.x + shadowPx, center.y + shadowPx), namePx, tag.voiceLevel, shadow);
                External::ImGUI::Widgets::DrawVoiceIcon(drawList, center, namePx, tag.voiceLevel, NametagColor(tag.color, alpha));
            }
            top += namePx;
        }

        if (!drawHealth) {
            return;
        }

        const float barTop = top + barGap + barBorder;
        const ImVec2 trackMin(screen.x - barWidth * 0.5f - barBorder, barTop - barBorder);
        const ImVec2 trackMax(screen.x + barWidth * 0.5f + barBorder, barTop + barHeight + barBorder);
        drawList->AddRectFilled(trackMin, trackMax, NametagColor(appearance.barTrackColor, alpha));

        const float fill = std::clamp(tag.healthPercent / 100.0f, 0.0f, 1.0f);
        if (fill > 0.0f) {
            const ImVec2 fillMin(screen.x - barWidth * 0.5f, barTop);
            drawList->AddRectFilled(fillMin, ImVec2(fillMin.x + barWidth * fill, barTop + barHeight), NametagHealthColor(tag.healthPercent, alpha));
        }
    }
} // namespace Framework::Integrations::Client::UI::Nametags
