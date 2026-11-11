/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/namespace.h"

#include <v8.h>

#include <string>

namespace Framework::Scripting {
    class LuaRuntime;
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {

    /**
     * Console that routes to the output of the runtime, for JavaScript and Lua.
     * The global console:
     * - console.log(), console.info() -> info level, with resource prefix
     * - console.warn() -> warning level
     * - console.error() -> error level
     * - console.debug() -> debug level
     *
     * JavaScript writes to the Framework logger. Lua writes where its print() does (LuaRuntime::WriteOutput).
     */
    class Console final {
      public:
        // The console, declared once for both languages.
        static const Bind::Namespace &Definition();

        /**
         * Register the console override in a context.
         * @param isolate V8 isolate
         * @param context Target context
         * @param resourceManager Resource manager for getting current resource context; null leaves the isolate with
         *        the manager it answers with already (Bind::SetCallResourceManager)
         */
        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, ResourceManager *resourceManager);

        // Publishes the console as a global of a Lua runtime.
        static void RegisterLua(LuaRuntime &runtime);

        // Format a single script value as the console writes it, in either language
        static std::string FormatValue(const Bind::Value &value);

        // Helper to format arguments as a string (public for testing)
        static std::string FormatArgs(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &args);

        // Format a single V8 value as a string (public for testing)
        static std::string FormatValue(v8::Isolate *isolate, v8::Local<v8::Value> value);
    };

} // namespace Framework::Scripting::Builtins
