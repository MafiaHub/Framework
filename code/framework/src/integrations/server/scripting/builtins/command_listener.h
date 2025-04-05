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
      public:
        static void Register(sol::state *luaEngine) {
            sol::usertype<Utils::CommandListener> cls = luaEngine->new_usertype<Utils::CommandListener>("CommandListener");
            
            // Register the command listener in the Server table
            (*luaEngine)["Server"]["CommandListener"] = CoreModules::GetCommandListener();
            
            // Register the RegisterCommand function
            (*luaEngine)["Server"]["RegisterCommand"] = [](const std::string &name, sol::function callback) {
                // Register a custom command handler
                CoreModules::GetScriptingEngine()->RegisterEvent("onServerCommand_" + name, callback);
            };
        }
    };
} // namespace Framework::Integrations::Scripting
