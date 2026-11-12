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
} // namespace Framework::Scripting

namespace Framework::Integrations::Server::Scripting::Builtins {

    // Server-side proximity voice rules, exposed as the global Voice to JavaScript and Lua. Players are
    // addressed through the base Entity handle, like the Chat builtin, so this stays game-agnostic.
    class Voice final {
      public:
        // Voice, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        // Publishes Voice as a global of a Lua runtime.
        static void RegisterLua(Framework::Scripting::LuaRuntime &runtime);
    };

} // namespace Framework::Integrations::Server::Scripting::Builtins
