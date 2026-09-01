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

    // The local player's own voice settings, exposed as the global Voice, for a resource
    // building a settings menu. The audibility radius belongs to the server's Voice builtin;
    // getRange reports it and setHearingRange can only narrow it locally.
    class Voice final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> target, Framework::Scripting::ResourceManager *resourceManager);

      private:
        static void SetEnabledCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void IsEnabledCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetVolumeCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void GetVolumeCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetHearingRangeCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void GetHearingRangeCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void GetRangeCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetPushToTalkKeyCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void GetPushToTalkKeyCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetPushToTalkReleaseDelayCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void GetPushToTalkReleaseDelayCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void IsTalkingCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void HasMicrophoneCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
