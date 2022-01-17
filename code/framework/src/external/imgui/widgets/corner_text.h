/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <imgui/imgui.h>
#include <fmt/format.h>

#include <string>

namespace Framework::External::ImGUI::Widgets {
    enum Corner {
        CORNER_LEFT_TOP,
        CORNER_RIGHT_TOP,
        CORNER_LEFT_BOTTOM,
        CORNER_RIGHT_BOTTOM,
    };
    // Adapted from Mafia: Oakwood Multiplayer
    static inline void DrawCornerText(Corner corner, const std::string &text, bool shadow = true) {
        constexpr float padding               = 2.0f;
        ImGuiIO &io                   = ImGui::GetIO();
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
                                        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (corner != -1) {
            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImVec2 work_pos               = viewport->WorkPos;
            ImVec2 work_size              = viewport->WorkSize;
            ImVec2 window_pos, window_pos_pivot;
            window_pos.x       = (corner & 1) ? (work_pos.x + work_size.x - padding) : (work_pos.x + padding);
            window_pos.y       = (corner & 2) ? (work_pos.y + work_size.y - padding) : (work_pos.y + padding);
            window_pos_pivot.x = (corner & 1) ? 1.0f : 0.0f;
            window_pos_pivot.y = (corner & 2) ? 1.0f : 0.0f;
            ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
            window_flags |= ImGuiWindowFlags_NoMove;
        }
        ImGui::SetNextWindowBgAlpha(0.35f);

        const auto windowName = fmt::format("Overlay #{}", corner);

        if (ImGui::Begin(windowName.c_str(), nullptr, window_flags)) {
            if (shadow)
                ImGui::PushFontShadow(0xFF000000);
            ImGui::Text("%s", text.c_str());
            if (shadow)
                ImGui::PopFontShadow();
        }
        ImGui::End();
    }
};
