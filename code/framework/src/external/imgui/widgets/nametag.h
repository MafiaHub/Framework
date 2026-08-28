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
#include <cstdint>

namespace Framework::External::ImGUI::Widgets {
    // Wire values are shared with Networking::Replication::NametagComponent; keep in sync.
    enum class NameTagComponent : uint8_t {
        Name   = 1 << 0,
        Health = 1 << 1,
    };

    // The viewer's own switches over every tag they see, driven by the client Nametags builtin.
    struct NameTagView {
        inline static bool showTags   = true;
        inline static bool showHealth = true;
    };

    // Distance behaviour of a world-anchored tag.
    struct NameTagLayout {
        float drawDistance = 50.0f; // hard cull
        float fadeStart    = 35.0f; // alpha ramp start; <=0 derives from drawDistance
        float nearScale    = 1.35f;
        float farScale     = 0.75f;
        float scaleStart   = 10.0f; // scale ramp runs nearScale -> farScale between these
        float scaleEnd     = 70.0f;
        float shiftStart   = 15.0f; // distance where the pixel lift starts
        float shiftMax     = 20.0f; // pixels, at drawDistance
    };

    inline bool NameTagHasComponent(uint8_t components, NameTagComponent component) {
        return (components & static_cast<uint8_t>(component)) != 0;
    }

    // The fade a tag would draw at, 0 when culled. Cheap pre-check before a bone lookup or projection.
    inline float NameTagAlpha(float distance, uint8_t components, const NameTagLayout &layout = {}) {
        if (!NameTagView::showTags || !NameTagHasComponent(components, NameTagComponent::Name)) {
            return 0.0f;
        }
        return WorldTextAlpha(distance, layout.drawDistance, layout.fadeStart);
    }

    enum class NameTagAnchor {
        TextCenter,
        BottomCenter,
    };

    struct NameTagStyle {
        ImU32 textColor       = IM_COL32(255, 255, 255, 255);
        ImU32 bgColor         = IM_COL32(0, 0, 0, 153);
        float fontSize        = 0.0f; // 0 = current font size
        float rounding        = 4.0f;
        float padding         = 4.0f;
        float healthBarWidth  = 50.0f; // used when a healthPercent is supplied
        float healthBarHeight = 5.0f;
        NameTagAnchor anchor  = NameTagAnchor::TextCenter;
    };

    // BottomCenter anchors the full widget, including the health bar.
    inline void DrawNameTag(ImDrawList *drawList, ImVec2 screenPos, const char *name, const NameTagStyle &style = {}, float alpha = 1.0f, float healthPercent = -1.0f) {
        if (!drawList || !name || !name[0] || alpha <= 0.0f) {
            return;
        }

        ImFont *font   = ImGui::GetFont();
        float fontSize = style.fontSize > 0.0f ? style.fontSize : ImGui::GetFontSize();

        const bool drawHealth    = healthPercent >= 0.0f && style.healthBarWidth > 0.0f && style.healthBarHeight > 0.0f;
        const ImVec2 textSize    = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name);
        const float bottomOffset = style.anchor == NameTagAnchor::BottomCenter ? textSize.y * 0.5f + style.padding + (drawHealth ? 3.0f + style.healthBarHeight : 0.0f) : 0.0f;
        const ImVec2 textPos(screenPos.x - textSize.x * 0.5f, screenPos.y - textSize.y * 0.5f - bottomOffset);

        drawList->AddRectFilled(ImVec2(textPos.x - style.padding, textPos.y - style.padding), ImVec2(textPos.x + textSize.x + style.padding, textPos.y + textSize.y + style.padding), WorldTextModulateAlpha(style.bgColor, alpha), style.rounding);
        drawList->AddText(font, fontSize, textPos, WorldTextModulateAlpha(style.textColor, alpha), name);

        if (!drawHealth) {
            return;
        }

        const float t            = std::clamp(healthPercent / 100.0f, 0.0f, 1.0f);
        const float halfBarWidth = style.healthBarWidth * 0.5f;
        const float barTop       = textPos.y + textSize.y + style.padding + 3.0f;
        const ImVec2 barMin(screenPos.x - halfBarWidth, barTop);
        const ImVec2 barMax(screenPos.x + halfBarWidth, barTop + style.healthBarHeight);

        drawList->AddRectFilled(barMin, barMax, WorldTextModulateAlpha(IM_COL32(0, 0, 0, 175), alpha), style.rounding * 0.5f);

        const ImU32 fillLeft  = WorldTextModulateAlpha(IM_COL32(80, 0, 0, 255), alpha);
        const ImU32 fillRight = WorldTextModulateAlpha(IM_COL32(80 + static_cast<int>(175.0f * t), static_cast<int>(40.0f * t), static_cast<int>(80.0f * t), 255), alpha);
        drawList->AddRectFilledMultiColor(barMin, ImVec2(barMin.x + style.healthBarWidth * t, barMax.y), fillLeft, fillRight, fillRight, fillLeft);
    }

    // Draw a replicated avatar's tag at a projected screen position, fading, scaling and lifting it
    // with distance. Returns false when culled; healthPercent < 0 draws no bar.
    inline bool DrawNameTagAt(ImDrawList *drawList, ImVec2 screenPos, float distance, const char *name, uint8_t components, ImU32 color, float healthPercent = -1.0f, const NameTagLayout &layout = {}, NameTagStyle style = {}) {
        const float alpha = NameTagAlpha(distance, components, layout);
        if (alpha <= 0.0f) {
            return false;
        }

        const float scaleSpan   = layout.scaleEnd - layout.scaleStart;
        const float scaleFactor = scaleSpan > 0.0f ? std::clamp((distance - layout.scaleStart) / scaleSpan, 0.0f, 1.0f) : 0.0f;
        const float scale       = layout.nearScale + (layout.farScale - layout.nearScale) * scaleFactor;

        style.textColor = color;
        style.fontSize  = (style.fontSize > 0.0f ? style.fontSize : ImGui::GetFontSize()) * scale;
        style.rounding *= scale;
        style.padding *= scale;
        style.healthBarWidth *= scale;
        style.healthBarHeight *= scale;

        const float shiftSpan   = layout.drawDistance - layout.shiftStart;
        const float shiftFactor = shiftSpan > 0.0f ? std::clamp((distance - layout.shiftStart) / shiftSpan, 0.0f, 1.0f) : 0.0f;
        screenPos.y -= shiftFactor * layout.shiftMax;

        const bool drawHealth = NameTagView::showHealth && NameTagHasComponent(components, NameTagComponent::Health);
        DrawNameTag(drawList, screenPos, name, style, alpha, drawHealth ? healthPercent : -1.0f);
        return true;
    }
} // namespace Framework::External::ImGUI::Widgets
