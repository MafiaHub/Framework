/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
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

    // Global Chat: send(text), setUIVisible(bool)/isUIVisible(), open()/close()/isOpen(). Incoming
    // lines arrive as the reserved "chatMessage" event. Overlay verbs drive the Instance's ChatBox.
    class Chat final {
      public:
        static void Register(v8::Isolate *isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Object> frameworkObj,
                             Framework::Scripting::ResourceManager *resourceManager);

      private:
        static void SendCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetUIVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void IsUIVisibleCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void OpenCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void CloseCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void IsOpenCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
