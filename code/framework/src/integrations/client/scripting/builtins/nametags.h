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

    // The local player's view of everyone's nametags, exposed as the global Nametags to JavaScript and Lua.
    // Local only; Player.setNametag* is the server-side counterpart and a tag draws only when both allow it.
    // setLabel hangs a transient line (speech, an emote, a status) on one entity's tag, for this
    // viewer alone -- the replicated half of a tag is Player.setNametagText.
    class Nametags final {
      public:
        // Nametags, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        // Publishes Nametags as a global in the languages of `languages`, in the runtimes of a client.
        static void Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages = Framework::Scripting::Bind::Languages::All, Framework::Scripting::ResourceManager *resourceManager = nullptr);

        // Drops every label: they are keyed by network id, which means nothing past the session.
        static void Shutdown();
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
