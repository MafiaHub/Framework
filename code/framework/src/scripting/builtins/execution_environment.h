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

#include <string>
#include <v8.h>

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
         * Publish the ExecutionEnvironment object as a global in the languages of `languages`. The side of each
         * runtime says which one it is.
         * @param registrar Where the builtins are registered
         * @param languages The languages to register it in
         * @param modVersion Local application's mod version; empty if unspecified
         */
        static void Register(Bind::Registrar &registrar, Bind::Languages languages = Bind::Languages::All, const std::string &modVersion = {});
    };

} // namespace Framework::Scripting::Builtins
