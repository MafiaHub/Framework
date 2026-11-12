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
    void RegisterSharedBuiltins(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> global, Framework::Scripting::ResourceManager *manager, const std::string &modVersion) {
        Framework::Scripting::SetScriptingEnvironment(isolate, /*isClient*/ true);

        // The events the framework raises; a mod's catalog blends them into its own EventMap.
        Framework::Scripting::RegisterEventMetadata(Framework::Scripting::GetScriptingCatalog(isolate));

        // The engine installs the timers without a binding; declare them for it.
        Framework::Scripting::RegisterTimerMetadata(Framework::Scripting::GetScriptingCatalog(isolate), /*isClient*/ true);

        // Every builtin registers at the global root (new Vector3, not new Core.Vector3).
        Framework::Scripting::Builtins::RegisterValueTypes(isolate, global);

        manager->GetEvents().Register(isolate, context, global, manager, /*isClient*/ true);
        Framework::Scripting::Builtins::Messages::Register(isolate, context, global, manager);
        Framework::Scripting::Builtins::Exports::Register(isolate, context, global, manager);
        Framework::Scripting::Builtins::Imports::Register(isolate, context, global, manager);
        Framework::Scripting::Builtins::Console::Register(isolate, context, manager);
        Framework::Scripting::Builtins::ExecutionEnvironment::Register(isolate, context, global, true, modVersion);
    }

    void RegisterClientBuiltins(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> global, Framework::Scripting::ResourceManager *manager) {
        Web::Register(isolate, context, global, manager);
        Keybinds::Register(isolate, context, global, manager);
        Chat::Register(isolate, context, global, manager);
        Discord::Register(isolate, context, global, manager);
        Voice::Register(isolate, context, global, manager);
        Nametags::Register(isolate, context, global, manager);
    }

    void RegisterClientBuiltinsLua(Framework::Scripting::LuaRuntime &runtime) {
        Web::RegisterLua(runtime);
        Keybinds::RegisterLua(runtime);
        Chat::RegisterLua(runtime);
        Discord::RegisterLua(runtime);
        Voice::RegisterLua(runtime);
        Nametags::RegisterLua(runtime);
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
