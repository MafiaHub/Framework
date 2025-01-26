/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "ui_base.h"

namespace Framework::External::ImGUI::Widgets {
    void UIBase::Open(const bool lockControls) {
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

    void UIBase::Toggle(const bool lockControls) {
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

    void UIBase::CleanUpUIWindow() const {
        if (!AreControlsLocked()) {

            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
        }

        ImGui::End();
    }

    void UIBase::CreateUIWindow(const char *name, const WindowContent windowContent, bool *pOpen, ImGuiWindowFlags flags) const {
        if (!AreControlsLocked()) {
            flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

            ImGui::SetNextWindowBgAlpha(_styleWindowBackgroundAlphaWhenControlsAreUnlocked);

            const ImGuiStyle &style = ImGui::GetStyle();
            ImGui::PushStyleColor(ImGuiCol_TitleBg, style.Colors[ImGuiCol_TitleBgCollapsed]);
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, style.Colors[ImGuiCol_TitleBgCollapsed]);
        }

        // If the clean up if the window was processed
        if (!ImGui::Begin(name, AreControlsLocked() ? pOpen : nullptr, flags)) {
            CleanUpUIWindow();
            return;
        }

        windowContent();

        CleanUpUIWindow();
    }
} // namespace Framework::External::ImGUI::Widgets
