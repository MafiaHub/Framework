/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/namespace.h"

#include <string>
#include <v8.h>

namespace Framework::Scripting {
    class LuaRuntime;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {

    /**
     * ExecutionEnvironment API - provides runtime environment information to JavaScript and Lua.
     *
     * Exposes an ExecutionEnvironment global object:
     * - ExecutionEnvironment.isClient - true if running on the client side (read-only)
     * - ExecutionEnvironment.isServer - true if running on the server side (read-only)
     * - ExecutionEnvironment.frameworkVersion - local Framework release (read-only)
     * - ExecutionEnvironment.modVersion - local multiplayer mod version (read-only)
     */
    class ExecutionEnvironment final {
      public:
        // The object, declared once for both languages. The side and the mod version are what each runtime is told
        // as it is registered.
        static const Bind::Namespace &Definition();

        /**
         * Register the ExecutionEnvironment object on the target object.
         * @param isolate V8 isolate
         * @param context Target context
         * @param target Object to attach ExecutionEnvironment to (e.g., the global root)
         * @param isClient true if this is the client side, false for server
         * @param modVersion Local application's mod version; empty if unspecified
         */
        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, bool isClient, const std::string &modVersion = {});

        /**
         * Publish the ExecutionEnvironment object as a global of a Lua runtime, whose side says which one it is.
         * @param runtime Lua runtime
         * @param modVersion Local application's mod version; empty if unspecified
         */
        static void RegisterLua(LuaRuntime &runtime, const std::string &modVersion = {});
    };

} // namespace Framework::Scripting::Builtins
