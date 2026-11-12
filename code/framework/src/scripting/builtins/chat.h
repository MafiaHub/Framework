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

namespace Framework::Scripting {
    class LuaRuntime;
} // namespace Framework::Scripting

namespace Framework::Scripting::Builtins {
    // Global Chat, sending the framework ChatMessage RPC, for JavaScript and Lua. Targets players through the
    // base Entity handle (a mod's Player/Human resolves to Entity through inheritance), so it stays game-agnostic.
    //   Chat.sendToAll(text, opts?)             opts = { author?: string, color?: Color | number 0xRRGGBBAA }
    //   Chat.sendToPlayer(player, text, opts?)
    //
    // To JavaScript Chat is a plain object, which a project may add functions of its own to. In Lua it is a
    // table a script reads and cannot write to, as every namespace is there.
    class Chat final {
      public:
        // Chat, declared once for both languages.
        static const Bind::Namespace &Definition();

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        // Publishes Chat as a global of a Lua runtime.
        static void RegisterLua(LuaRuntime &runtime);
    };
} // namespace Framework::Scripting::Builtins
