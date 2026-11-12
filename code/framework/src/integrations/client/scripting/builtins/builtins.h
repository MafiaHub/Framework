/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <scripting/bind/descriptors.h>

#include <v8.h>

#include <string>
#include <vector>

namespace Framework::Scripting {
    class LuaRuntime;
    class ResourceManager;
} // namespace Framework::Scripting

// The builtins only a client has, named once: Web, Key, Chat, Discord, Voice and Nametags. The client scripting
// module registers them in its isolate and in every Lua state through these, and writes its Lua definition file
// from the same list, so a builtin added here is in both languages and in the file. What the isolate of a client
// has before them is named here too. It is apart from the module,
// which builds on Windows only, so that the tests hold exactly this list to the parity gate.
//
// Nametags needs the headers of ImGui. A test binary built where they are not defines
// FW_CLIENT_SCRIPTING_WITHOUT_NAMETAGS and has every other builtin; nothing else may define it.
namespace Framework::Integrations::Client::Scripting::Builtins {
    // Registers in the isolate what a client shares with a server, in the client's flavour: the environment, the
    // metadata of the events and the timers, the value types, Events, Messages, Exports, Imports, Console and
    // the execution environment. Call with `context` entered, once the catalog of the isolate is set
    // (SetScriptingCatalog) and before RegisterClientBuiltins. It is here, and not in the module, so that a
    // shared builtin added to a client is one the parity gate sees.
    void RegisterSharedBuiltins(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> global, Framework::Scripting::ResourceManager *manager, const std::string &modVersion);

    // Registers the client builtins in the isolate, after the shared ones. Call with `context` entered.
    void RegisterClientBuiltins(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> global, Framework::Scripting::ResourceManager *manager);

    // Publishes the client builtins in a Lua state, after the shared ones: the runtime setup of the Lua host.
    void RegisterClientBuiltinsLua(Framework::Scripting::LuaRuntime &runtime);

    // The definitions of everything a Lua state of a client has: the shared ones, then those of the client
    // builtins in the order they are registered. What the Lua definition file of a client is written from.
    std::vector<const Framework::Scripting::Bind::ClassDesc *> ClientLuaDefinitions();
} // namespace Framework::Integrations::Client::Scripting::Builtins
