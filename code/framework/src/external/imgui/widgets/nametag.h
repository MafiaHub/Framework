/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "world_text.h"

#include <imgui/imgui.h>

#include <algorithm>

namespace Framework::External::ImGUI::Widgets {
    struct NameTagStyle {
        ImU32 textColor       = IM_COL32(255, 255, 255, 255);
        ImU32 bgColor         = IM_COL32(0, 0, 0, 153);
        float fontSize        = 0.0f; // 0 = current font size
        float rounding        = 4.0f;
        float padding         = 4.0f;
        float healthBarWidth  = 50.0f; // used when a healthPercent is supplied
        float healthBarHeight = 5.0f;
    };

    // Draw a name tag centered on screenPos (pixels): the name over a rounded background box, and an
    // optional health bar below it when healthPercent is in [0, 100]. color alphas are multiplied by
    // `alpha` (distance fade, see WorldTextAlpha).
    inline void DrawNameTag(ImDrawList *drawList, ImVec2 screenPos, const char *name, const NameTagStyle &style = {}, float alpha = 1.0f, float healthPercent = -1.0f) {
        if (!drawList || !name || !name[0] || alpha <= 0.0f) {
            return;
        }

        ImFont *font   = ImGui::GetFont();
        float fontSize = style.fontSize > 0.0f ? style.fontSize : ImGui::GetFontSize();

        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name);
        const ImVec2 textPos(screenPos.x - textSize.x * 0.5f, screenPos.y - textSize.y * 0.5f);

        drawList->AddRectFilled(
            ImVec2(textPos.x - style.padding, textPos.y - style.padding),
            ImVec2(textPos.x + textSize.x + style.padding, textPos.y + textSize.y + style.padding),
            WorldTextModulateAlpha(style.bgColor, alpha),
            style.rounding);
        drawList->AddText(font, fontSize, textPos, WorldTextModulateAlpha(style.textColor, alpha), name);

        if (healthPercent < 0.0f || style.healthBarWidth <= 0.0f || style.healthBarHeight <= 0.0f) {
            return;
        }

        const float t             = std::clamp(healthPercent / 100.0f, 0.0f, 1.0f);
        const float halfBarWidth  = style.healthBarWidth * 0.5f;
        const float barTop        = textPos.y + textSize.y + style.padding + 3.0f;
        const ImVec2 barMin(screenPos.x - halfBarWidth, barTop);
        const ImVec2 barMax(screenPos.x + halfBarWidth, barTop + style.healthBarHeight);

        drawList->AddRectFilled(barMin, barMax, WorldTextModulateAlpha(IM_COL32(0, 0, 0, 175), alpha), style.rounding * 0.5f);

        const ImU32 fillLeft  = WorldTextModulateAlpha(IM_COL32(80, 0, 0, 255), alpha);
        const ImU32 fillRight = WorldTextModulateAlpha(
            IM_COL32(80 + static_cast<int>(175.0f * t), static_cast<int>(40.0f * t), static_cast<int>(80.0f * t), 255), alpha);
        drawList->AddRectFilledMultiColor(barMin, ImVec2(barMin.x + style.healthBarWidth * t, barMax.y), fillLeft, fillRight, fillRight, fillLeft);
    }
} // namespace Framework::External::ImGUI::Widgets
