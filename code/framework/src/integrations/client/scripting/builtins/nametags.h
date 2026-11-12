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

    // The local player's view of everyone's nametags, exposed as the global Nametags to JavaScript and Lua.
    // Local only; Player.setNametag* is the server-side counterpart and a tag draws only when both allow it.
    // setLabel hangs a transient line (speech, an emote, a status) on one entity's tag, for this
    // viewer alone -- the replicated half of a tag is Player.setNametagText.
    class Nametags final {
      public:
        // Nametags, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager);

        // Publishes Nametags as a global of a Lua runtime.
        static void RegisterLua(Framework::Scripting::LuaRuntime &runtime);

        // Drops every label: they are keyed by network id, which means nothing past the session.
        static void Shutdown();
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
