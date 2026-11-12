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

namespace Framework::Scripting {
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

        // Publishes Voice as a global in the languages of `languages`, in the runtimes of a client.
        static void Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages = Framework::Scripting::Bind::Languages::All, Framework::Scripting::ResourceManager *resourceManager = nullptr);
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
