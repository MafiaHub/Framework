/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
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

    // Client-side Discord rich presence API (global Discord), for JavaScript and Lua; see
    // types/framework/discord.d.ts. Setters stage onto a pending activity; update()/setPresence() publish it
    // as the script layer over the mod's own activity (see Wrapper::SetScriptFields). The staged activity is
    // one for the client, whatever the language and the resource of the scripts that write it.
    class Discord final {
      public:
        // Discord, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager);

        // Publishes Discord as a global of a Lua runtime.
        static void RegisterLua(Framework::Scripting::LuaRuntime &runtime);

        static void Shutdown();
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
