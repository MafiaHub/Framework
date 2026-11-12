/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "builtins.h"

#include "chat.h"
#include "discord.h"
#include "keybinds.h"
#include "nametags.h"
#include "voice.h"
#include "web.h"

#include <scripting/builtins/builtins.h>
#include <scripting/builtins/console.h>
#include <scripting/builtins/events.h>
#include <scripting/builtins/execution_environment.h>
#include <scripting/builtins/exports.h>
#include <scripting/builtins/imports.h>
#include <scripting/builtins/messages.h>
#include <scripting/event_metadata.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/scripting_catalog.h>
#include <scripting/timer_metadata.h>

namespace Framework::Integrations::Client::Scripting::Builtins {
    void RegisterSharedBuiltins(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::ResourceManager &manager, const std::string &modVersion) {
        if (!registrar.HasJavaScript()) {
            return;
        }
        v8::Isolate *isolate = registrar.Isolate();
        Framework::Scripting::SetScriptingEnvironment(isolate, /*isClient*/ true);

        // The events the framework raises; a mod's catalog blends them into its own EventMap.
        Framework::Scripting::RegisterEventMetadata(Framework::Scripting::GetScriptingCatalog(isolate));

        // The engine installs the timers without a binding; declare them for it.
        Framework::Scripting::RegisterTimerMetadata(Framework::Scripting::GetScriptingCatalog(isolate), /*isClient*/ true);

        // Every builtin registers at the global root (new Vector3, not new Core.Vector3). The Lua host registers
        // the same builtins in Lua (Framework::Scripting::Builtins::RegisterShared).
        const Framework::Scripting::Bind::Languages languages = Framework::Scripting::Bind::Languages::JavaScript;
        Framework::Scripting::Builtins::RegisterValueTypes(registrar, languages);

        manager.GetEvents().Register(registrar, languages, &manager);
        Framework::Scripting::Builtins::Messages::Register(registrar, languages, &manager);
        Framework::Scripting::Builtins::Exports::Register(registrar, languages, &manager);
        Framework::Scripting::Builtins::Imports::Register(registrar, languages, &manager);
        Framework::Scripting::Builtins::Console::Register(registrar, languages, &manager);
        Framework::Scripting::Builtins::ExecutionEnvironment::Register(registrar, languages, modVersion);
    }

    void RegisterClientBuiltins(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages, Framework::Scripting::ResourceManager *manager) {
        Web::Register(registrar, languages, manager);
        Keybinds::Register(registrar, languages, manager);
        Chat::Register(registrar, languages, manager);
        Discord::Register(registrar, languages, manager);
        Voice::Register(registrar, languages, manager);
        Nametags::Register(registrar, languages, manager);
    }

    std::vector<const Framework::Scripting::Bind::ClassDesc *> ClientLuaDefinitions() {
        return Framework::Scripting::Builtins::SharedDefinitions({
            &Web::Definition().Describe(),
            &Keybinds::Definition().Describe(),
            &Chat::Definition().Describe(),
            &Discord::Definition().Describe(),
            &Voice::Definition().Describe(),
            &Nametags::Definition().Describe(),
        });
    }
} // namespace Framework::Integrations::Client::Scripting::Builtins
