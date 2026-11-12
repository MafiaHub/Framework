/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <scripting/bind/namespace.h>

#include <v8.h>

namespace Framework::Scripting {
    class LuaRuntime;
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Integrations::Client::Scripting::Builtins {

    // The local player's own voice settings, exposed as the global Voice to JavaScript and Lua, for a
    // resource building a settings menu. The audibility radius belongs to the server's Voice builtin;
    // getRange reports it and setHearingRange can only narrow it locally.
    class Voice final {
      public:
        // Voice, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager);

        // Publishes Voice as a global of a Lua runtime.
        static void RegisterLua(Framework::Scripting::LuaRuntime &runtime);
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
