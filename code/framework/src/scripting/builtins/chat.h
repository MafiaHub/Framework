/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>

namespace Framework::Scripting::Builtins {
    // Global Chat, sending the framework ChatMessage RPC. Targets players through the base Entity
    // handle (a mod's Player/Human resolves to Entity via v8pp inheritance), so it stays game-agnostic.
    //   Chat.sendToAll(text, opts?)             opts = { author?: string, color?: number 0xRRGGBBAA }
    //   Chat.sendToPlayer(player, text, opts?)
    class Chat final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_SendToAll(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SendToPlayer(const v8::FunctionCallbackInfo<v8::Value> &info);
    };
} // namespace Framework::Scripting::Builtins
