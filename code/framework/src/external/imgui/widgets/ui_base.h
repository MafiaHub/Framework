/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <fu2/function2.hpp>
#include <imgui/imgui.h>

namespace Framework::External::ImGUI::Widgets {
    class UIBase {
      public:
        using WindowContent = fu2::function<void() const>;

      private:
        bool _lockControlsWhenOpen = false;

        bool _wasOpen = false;

        void CleanUpUIWindow();

      protected:
        bool _open = false;

        float _styleWindowBackgroundAlphaWhenControlsAreUnlocked = 0.25f;

        unsigned int _styleFontShadowWhenControlsAreUnlocked = 0xFF000000;

        virtual void OnOpen() = 0;

        virtual void OnClose() = 0;

        virtual void OnUpdate() = 0;

        virtual bool AreControlsLocked() const = 0;

        virtual void LockControls(bool lock) const = 0;

      public:
        UIBase() {};

        bool IsOpen() const {
            return _open;
        }

        /**
         * You probably shouldn't override `Open`, use `OnOpen` instead.
         */
        void Open(bool lockControls = true);

        /**
         * You probably shouldn't override `Close`, use `OnClose` instead.
         */
        void Close();

        void Toggle(bool lockControls = true);

        /**
         * You probably shouldn't override `Update`, use `OnUpdate` instead.
         *
         * Update will prevent OnUpdate to be called if the UI is not open.
         */
        void Update();

        /**
         * Create a custom ImGui window.
         *
         * When the controls are unlocked:
         * - window background becomes transparent (customize with _styleWindowBackgroundAlphaWhenControlsAreUnlocked)
         * - add font shadow (customize with _styleFontShadowWhenControlsAreUnlocked)
         * - set title bar to "collapse" style
         * - close button is removed
         * - resizing is disabled
         * - collapsing is disabled
         */
        void CreateUIWindow(const char *name, WindowContent windowContent, bool *pOpen = NULL, ImGuiWindowFlags flags = 0);
    };
} // namespace Framework::External::ImGUI::Widgets
