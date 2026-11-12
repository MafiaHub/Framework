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
#ifndef FW_CLIENT_SCRIPTING_WITHOUT_NAMETAGS
#include "nametags.h"
#endif
#include "voice.h"
#include "web.h"

#include <scripting/builtins/builtins.h>

namespace Framework::Integrations::Client::Scripting::Builtins {
    void RegisterClientBuiltins(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> global, Framework::Scripting::ResourceManager *manager) {
        Web::Register(isolate, context, global, manager);
        Keybinds::Register(isolate, context, global, manager);
        Chat::Register(isolate, context, global, manager);
        Discord::Register(isolate, context, global, manager);
        Voice::Register(isolate, context, global, manager);
#ifndef FW_CLIENT_SCRIPTING_WITHOUT_NAMETAGS
        Nametags::Register(isolate, context, global, manager);
#endif
    }

    void RegisterClientBuiltinsLua(Framework::Scripting::LuaRuntime &runtime) {
        Web::RegisterLua(runtime);
        Keybinds::RegisterLua(runtime);
        Chat::RegisterLua(runtime);
        Discord::RegisterLua(runtime);
        Voice::RegisterLua(runtime);
#ifndef FW_CLIENT_SCRIPTING_WITHOUT_NAMETAGS
        Nametags::RegisterLua(runtime);
#endif
    }

    std::vector<const Framework::Scripting::Bind::ClassDesc *> ClientLuaDefinitions() {
        return Framework::Scripting::Builtins::SharedDefinitions({
            &Web::Definition().Describe(),
            &Keybinds::Definition().Describe(),
            &Chat::Definition().Describe(),
            &Discord::Definition().Describe(),
            &Voice::Definition().Describe(),
#ifndef FW_CLIENT_SCRIPTING_WITHOUT_NAMETAGS
            &Nametags::Definition().Describe(),
#endif
        });
    }
} // namespace Framework::Integrations::Client::Scripting::Builtins
