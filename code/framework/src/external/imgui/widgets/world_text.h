/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Framework::External::ImGUI::Widgets {
    // Wire values are shared with Networking::Replication::Entities::TextLabelStyle; keep in sync.
    enum class WorldTextStyle : uint8_t {
        None    = 0,
        Shadow  = 1,
        Outline = 2,
        Box     = 3,
    };

    // Linear distance fade: opaque up to fadeStart, gone at drawDistance. fadeStart outside
    // (0, drawDistance) derives to 75% of drawDistance.
    inline float WorldTextAlpha(float distance, float drawDistance, float fadeStart = 0.0f) {
        if (drawDistance <= 0.0f || distance >= drawDistance) {
            return 0.0f;
        }
        if (fadeStart <= 0.0f || fadeStart >= drawDistance) {
            fadeStart = drawDistance * 0.75f;
        }
        if (distance <= fadeStart) {
            return 1.0f;
        }
        return 1.0f - (distance - fadeStart) / (drawDistance - fadeStart);
    }

    inline ImU32 WorldTextModulateAlpha(ImU32 color, float alpha) {
        alpha         = std::clamp(alpha, 0.0f, 1.0f);
        const ImU32 a = static_cast<ImU32>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
        return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    }

    // Draw multi-line ('\n') UTF-8 text centered on screenPos (pixels). color is an ImU32 with its
    // own alpha; `alpha` multiplies on top (distance fade).
    inline void DrawWorldText(ImDrawList *drawList, ImVec2 screenPos, const char *text, ImU32 color, float fontSize, WorldTextStyle style, float alpha = 1.0f) {
        if (!drawList || !text || !text[0] || alpha <= 0.0f) {
            return;
        }

        ImFont *font = ImGui::GetFont();
        if (fontSize <= 0.0f) {
            fontSize = ImGui::GetFontSize();
        }

        // Measure pass: line count and widest line.
        int lineCount  = 0;
        float maxWidth = 0.0f;
        for (const char *line = text; line; ++lineCount) {
            const char *end   = strchr(line, '\n');
            const float width = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, line, end).x;
            if (width > maxWidth) {
                maxWidth = width;
            }
            line = end ? end + 1 : nullptr;
        }

        const float totalHeight = static_cast<float>(lineCount) * fontSize;
        const float top         = screenPos.y - totalHeight * 0.5f;

        const ImU32 mainColor  = WorldTextModulateAlpha(color, alpha);
        const ImU32 blackColor = WorldTextModulateAlpha(IM_COL32(0, 0, 0, (color >> IM_COL32_A_SHIFT) & 0xFF), alpha);

        if (style == WorldTextStyle::Box) {
            constexpr float kPadding = 4.0f;
            drawList->AddRectFilled(
                ImVec2(screenPos.x - maxWidth * 0.5f - kPadding, top - kPadding),
                ImVec2(screenPos.x + maxWidth * 0.5f + kPadding, top + totalHeight + kPadding),
                WorldTextModulateAlpha(IM_COL32(0, 0, 0, 153), alpha),
                4.0f);
        }

        int lineIndex = 0;
        for (const char *line = text; line; ++lineIndex) {
            const char *end   = strchr(line, '\n');
            const float width = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, line, end).x;
            const ImVec2 pos(screenPos.x - width * 0.5f, top + static_cast<float>(lineIndex) * fontSize);

            if (style == WorldTextStyle::Shadow) {
                drawList->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y + 1.0f), blackColor, line, end);
            }
            else if (style == WorldTextStyle::Outline) {
                drawList->AddText(font, fontSize, ImVec2(pos.x - 1.0f, pos.y), blackColor, line, end);
                drawList->AddText(font, fontSize, ImVec2(pos.x + 1.0f, pos.y), blackColor, line, end);
                drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y - 1.0f), blackColor, line, end);
                drawList->AddText(font, fontSize, ImVec2(pos.x, pos.y + 1.0f), blackColor, line, end);
            }

            drawList->AddText(font, fontSize, pos, mainColor, line, end);
            line = end ? end + 1 : nullptr;
        }
    }
} // namespace Framework::External::ImGUI::Widgets
