/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
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

    // Client-side Discord rich presence API (global Discord), for JavaScript and Lua; see
    // types/framework/discord.d.ts. Setters stage onto a pending activity; update()/setPresence() publish it
    // as the script layer over the mod's own activity (see Wrapper::SetScriptFields). The staged activity is
    // one for the client, whatever the language and the resource of the scripts that write it.
    class Discord final {
      public:
        // Discord, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        // Publishes Discord as a global in the languages of `languages`, in the runtimes of a client.
        static void Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages = Framework::Scripting::Bind::Languages::All, Framework::Scripting::ResourceManager *resourceManager = nullptr);

        static void Shutdown();
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
