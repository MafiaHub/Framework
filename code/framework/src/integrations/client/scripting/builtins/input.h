/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <sol/sol.hpp>

#include "core_modules.h"

#include <input/input.h>

namespace Framework::Integrations::Scripting {
    class Input {
        private:
        static glm::vec2 GetMousePosition() {
            int x, y;
            Framework::CoreModules::GetInput()->GetMousePosition(x, y);
            return glm::vec2(x, y);
        }

        static void SetMousePosition(int x, int y) {
            Framework::CoreModules::GetInput()->SetMousePosition(x, y);
        }

        static void SetMouseVisible(bool visible) {
            Framework::CoreModules::GetInput()->SetMouseVisible(visible);
        }

        static bool IsMouseVisible() {
            return Framework::CoreModules::GetInput()->IsMouseVisible();
        }

        static void SetMouseLocked(bool locked) {
            Framework::CoreModules::GetInput()->SetMouseLocked(locked);
        }

        static bool IsMouseLocked() {
            return Framework::CoreModules::GetInput()->IsMouseLocked();
        }

        static void SetInputLocked(bool locked) {
            Framework::CoreModules::GetInput()->SetInputLocked(locked);
        }

        static bool IsInputLocked() {
            return Framework::CoreModules::GetInput()->IsInputLocked();
        }

        static bool IsKeyDown(int key) {
            return Framework::CoreModules::GetInput()->IsKeyDown(key);
        }

        static bool IsKeyUp(int key) {
            return Framework::CoreModules::GetInput()->IsKeyUp(key);
        }

        static bool IsKeyPressed(int key) {
            return Framework::CoreModules::GetInput()->IsKeyPressed(key);
        }

        static bool IsKeyReleased(int key) {
            return Framework::CoreModules::GetInput()->IsKeyReleased(key);
        }

        static bool IsMouseButtonDown(int button) {
            return Framework::CoreModules::GetInput()->IsMouseButtonDown(button);
        }

        static bool IsMouseButtonUp(int button) {
            return Framework::CoreModules::GetInput()->IsMouseButtonUp(button);
        }

        static bool IsMouseButtonPressed(int button) {
            return Framework::CoreModules::GetInput()->IsMouseButtonPressed(button);
        }

        static bool IsMouseButtonReleased(int button) {
            return Framework::CoreModules::GetInput()->IsMouseButtonReleased(button);
        }

        public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<Input> cls = luaEngine->new_usertype<Input>("Input");

            cls["getMousePosition"]       = &Input::GetMousePosition;
            cls["setMousePosition"]       = &Input::SetMousePosition;
            cls["setMouseVisible"]        = &Input::SetMouseVisible;
            cls["getMouseVisible"]        = &Input::IsMouseVisible;
            cls["setMouseLocked"]         = &Input::SetMouseLocked;
            cls["getMouseLocked"]         = &Input::IsMouseLocked;
            cls["setInputLocked"]         = &Input::SetInputLocked;
            cls["getInputLocked"]         = &Input::IsInputLocked;
            cls["getKeyDown"]             = &Input::IsKeyDown;
            cls["getKeyUp"]               = &Input::IsKeyUp;
            cls["getKeyPressed"]          = &Input::IsKeyPressed;
            cls["getKeyReleased"]         = &Input::IsKeyReleased;
            cls["getMouseButtonDown"]     = &Input::IsMouseButtonDown;
            cls["getMouseButtonUp"]       = &Input::IsMouseButtonUp;
            cls["getMouseButtonPressed"]  = &Input::IsMouseButtonPressed;
            cls["getMouseButtonReleased"] = &Input::IsMouseButtonReleased;
        }
    };
} // namespace Framework::Integrations::Scripting
