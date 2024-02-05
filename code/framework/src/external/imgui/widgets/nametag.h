/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <fmt/format.h>
#include <imgui/imgui.h>

#include <string>

namespace Framework::External::ImGUI::Widgets {
    void DrawNameTag(ImVec2 screenPos, const char *label, float healthPercent, float healthBarWidth = 50.0f, float healthBarHeight = 8.0f) {
        ImDrawList *drawList = ImGui::GetBackgroundDrawList();

        const ImVec2 textSize     = ImGui::CalcTextSize(label);
        const float halfTextWidth = textSize.x / 2.0f;
        drawList->AddText(NULL, 16.0f, {screenPos.x - halfTextWidth + 1, screenPos.y + 1}, IM_COL32(0, 0, 0, 255), label);
        drawList->AddText(NULL, 16.0f, {screenPos.x - halfTextWidth, screenPos.y}, IM_COL32(255, 255, 255, 255), label);

        if (healthBarWidth <= 0.0f || healthBarHeight <= 0.0f)
            return;

        const float halfHealthBarWidth = healthBarWidth / 2.0f;
        screenPos.y += 1.2f * textSize.y;
        drawList->AddRect({screenPos.x - halfHealthBarWidth - 1.0f, screenPos.y}, {screenPos.x + halfHealthBarWidth + 1.0f, screenPos.y + healthBarHeight + 1.0f}, IM_COL32(100, 100, 100, 200));
        drawList->AddRectFilled({screenPos.x - halfHealthBarWidth, screenPos.y + 1.0f}, {screenPos.x + halfHealthBarWidth, screenPos.y + healthBarHeight}, IM_COL32(0, 0, 0, 175));

        const float alpha               = std::clamp(healthPercent / 100.0f, 0.0f, 1.0f);
        const ImU32 healthBarLeftColor  = IM_COL32(80, 0, 0, 255);
        const ImU32 healthBarRightColor = IM_COL32(glm::mix(80, 255, alpha), glm::mix(0, 40, alpha), glm::mix(0, 80, alpha), 255);
        const ImVec2 healthBarMin       = {screenPos.x - halfHealthBarWidth, screenPos.y + 1.0f};
        const ImVec2 healthBarMax       = {screenPos.x - halfHealthBarWidth + (alpha * healthBarWidth), screenPos.y + healthBarHeight};
        drawList->AddRectFilledMultiColor(healthBarMin, healthBarMax, healthBarLeftColor, healthBarRightColor, healthBarRightColor, healthBarLeftColor);
    }
} // namespace Framework::External::ImGUI::Widgets
