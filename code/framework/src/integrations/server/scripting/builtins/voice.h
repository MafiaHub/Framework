/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>

namespace Framework::Integrations::Server::Scripting::Builtins {

    // Server-side proximity voice rules, exposed as the global Voice. Players are addressed
    // through the base Entity handle, like the Chat builtin, so this stays game-agnostic.
    class Voice final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_SetRange(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetRange(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetPlayerRange(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetPlayerRange(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetPlayerMuted(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsPlayerMuted(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetPlayerDeaf(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsPlayerDeaf(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetLocalMute(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsLocallyMuted(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsPlayerVoiceEnabled(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsPlayerTalking(const v8::FunctionCallbackInfo<v8::Value> &info);
    };

} // namespace Framework::Integrations::Server::Scripting::Builtins
