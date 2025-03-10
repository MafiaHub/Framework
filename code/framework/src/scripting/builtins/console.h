/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/glm.hpp>
#include <sol/sol.hpp>

#include <string>

namespace Framework::Scripting::Builtins {
    class Console final {
        static void Log(sol::variadic_args args) {
            std::string str;

            for (auto arg : args) {
                // sol::variadic_args automatically converts arguments to strings
                // when using string concatenation
                str += arg.as<std::string>();
            }

            Framework::Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(str);
        }

      public:
        static void Register(sol::state &luaEngine) {
            sol::usertype<Console> cls = luaEngine.new_usertype<Console>("Console");
            cls["log"]                 = &Console::Log;
        }
    };
} // namespace Framework::Scripting::Builtins
