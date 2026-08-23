/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>

namespace Framework::Scripting {
    class ResourceManager;
} // namespace Framework::Scripting

namespace Framework::Integrations::Client::Scripting::Builtins {

    // The local player's view of everyone's nametags, exposed as the global Nametags. Local only;
    // Player.setNametag* is the server-side counterpart and a tag draws only when both allow it.
    class Nametags final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager);

      private:
        static void SetVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void IsVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetHealthVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void IsHealthVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
