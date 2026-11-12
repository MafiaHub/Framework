/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <scripting/bind/namespace.h>
#include <scripting/bind/registrar.h>

#include <v8.h>

namespace Framework::Integrations::Server::Scripting::Builtins {

    // Server-side proximity voice rules, exposed as the global Voice to JavaScript and Lua. Players are
    // addressed through the base Entity handle, like the Chat builtin, so this stays game-agnostic.
    class Voice final {
      public:
        // Voice, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        // Publishes Voice as a global in the languages of `languages`, in the runtimes of a server. JavaScript
        // also gets the voice events in its catalog.
        static void Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages = Framework::Scripting::Bind::Languages::All);
    };

} // namespace Framework::Integrations::Server::Scripting::Builtins
