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
            
            // Add methods to the CommandListener usertype
            cls["RegisterCommand"] = [](Utils::CommandListener*, const std::string &name, sol::function callback) {
                // Register a custom command handler
                if (CoreModules::GetScriptingEngine()) {
                    CoreModules::GetScriptingEngine()->RegisterEvent("onServerCommand_" + name, callback);
                }
            };
            
            // Register the command listener in the Server table if available
            if (CoreModules::GetCommandListener()) {
                (*luaEngine)["Server"]["CommandListener"] = CoreModules::GetCommandListener();
            }
        }
    };
} // namespace Framework::Integrations::Scripting
