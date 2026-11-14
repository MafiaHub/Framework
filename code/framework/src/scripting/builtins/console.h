/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../bind/namespace.h"
#include "../bind/registrar.h"

#include <v8.h>

#include <string>

namespace Framework::Scripting {
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
     * JavaScript writes to the Framework logger. Lua writes where its print() does, so
     * a line of it is cut at the bound of a printed one, and the arguments past that bound are not formatted.
     *
     * console.info is console.log itself in JavaScript, as it always was. In Lua they are two functions.
     */
    class Console final {
      public:
        // The console, declared once for both languages.
        static const Bind::Namespace &Definition();

        /**
         * Publish the console as a global in the languages of `languages`.
         * @param registrar Where the builtins are registered
         * @param languages The languages to register it in
         * @param resourceManager Resource manager for getting current resource context. In JavaScript the console
         *        asks the manager its isolate answers with (Bind::SetCallResourceManager), which this sets. Null
         *        sets none: the console then names its resource through the manager the isolate was given before,
         *        or the scripting module's, where a console registered without a manager used to write without a
         *        name. A Lua runtime is told its manager by the scripting module.
         */
        static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All, ResourceManager *resourceManager = nullptr);

        // Format a single script value as the console writes it, in either language
        static std::string FormatValue(const Bind::Value &value);

        // Helper to format arguments as a string (public for testing)
        static std::string FormatArgs(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &args);

        // Format a single V8 value as a string (public for testing)
        static std::string FormatValue(v8::Isolate *isolate, v8::Local<v8::Value> value);
    };

} // namespace Framework::Scripting::Builtins
