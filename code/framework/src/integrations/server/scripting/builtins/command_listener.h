/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <sol/sol.hpp>

#include "utils/command_listener.h"
#include "core_modules.h"

namespace Framework::Integrations::Scripting {
    class CommandListenerBuiltin {
      private:
        static void RegisterCommand(const std::string &name, const sol::function &callback) {
            if (CoreModules::GetScriptingEngine()) {
                CoreModules::GetScriptingEngine()->RegisterEvent("onServerCommand_" + name, callback);
            }
        }

      public:
        static void Register(sol::state *luaEngine) {
            // Register the CommandListener functions in the Server table
            (*luaEngine)["Server"]["RegisterCommand"] = &CommandListenerBuiltin::RegisterCommand;
        }
    };
} // namespace Framework::Integrations::Scripting
