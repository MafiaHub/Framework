/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "ui_base.h"

namespace Framework::External::ImGUI::Widgets {
    void UIBase::Open(bool lockControls) {
        _open    = true;
        _wasOpen = true;

        if (lockControls) {
            _lockControlsWhenOpen = true;
            LockControls(true);
        }

        OnOpen();
    }

    void UIBase::Close() {
        OnClose();

        if (_lockControlsWhenOpen) {
            LockControls(false);
        }

        _lockControlsWhenOpen = false;
        _wasOpen              = false;
        _open                 = false;
    }

    void UIBase::Toggle(bool lockControls) {
        if (_open) {
            Close();
        }
        else {
            Open(lockControls);
        }
    }

    void UIBase::Update() {
        // Detect if _open has changed but Close() has not been called.
        // This can be the case when we use the close button of the window.
        if (_wasOpen && !_open) {
            Close();
            return;
        }

        // Prevent updating if not open
        if (!_open) {
            return;
        }

        OnUpdate();
    }

    void UIBase::CleanUpUIWindow() {
        if (!AreControlsLocked()) {
            ImGui::PopFontShadow();

            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
        }

        ImGui::End();
    }

    void UIBase::CreateUIWindow(const char *name, WindowContent windowContent, bool *pOpen, ImGuiWindowFlags flags) {
        if (!AreControlsLocked()) {
            flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

            ImGui::SetNextWindowBgAlpha(_styleWindowBackgroundAlphaWhenControlsAreUnlocked);
            ImGui::PushFontShadow(_styleFontShadowWhenControlsAreUnlocked);

            ImGuiStyle &style = ImGui::GetStyle();
            ImGui::PushStyleColor(ImGuiCol_TitleBg, style.Colors[ImGuiCol_TitleBgCollapsed]);
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, style.Colors[ImGuiCol_TitleBgCollapsed]);
        }

        bool wasWindowProcessed = ImGui::Begin(name, AreControlsLocked() ? pOpen : NULL, flags);

        if (!wasWindowProcessed) {
            CleanUpUIWindow();
            return;
        }

        windowContent();

        CleanUpUIWindow();
    }
} // namespace Framework::External::ImGUI::Widgets
