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

    // Global Chat of a client, for JavaScript and Lua: send(text), setUIVisible(bool)/isUIVisible(),
    // open()/close()/isOpen(). Incoming lines arrive as the reserved "chatMessage" event; typed lines are
    // first offered to "chatSend", which blocks the send by returning false. Overlay verbs drive the
    // Instance's ChatBox. The server has a Chat of its own (scripting/builtins/chat.h): another definition
    // under the same name, which no runtime has together with this one.
    class Chat final {
      public:
        // Chat, declared once for both languages.
        static const Framework::Scripting::Bind::Namespace &Definition();

        // Publishes Chat as a global in the languages of `languages`, in the runtimes of a client.
        static void Register(Framework::Scripting::Bind::Registrar &registrar, Framework::Scripting::Bind::Languages languages = Framework::Scripting::Bind::Languages::All, Framework::Scripting::ResourceManager *resourceManager = nullptr);
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
