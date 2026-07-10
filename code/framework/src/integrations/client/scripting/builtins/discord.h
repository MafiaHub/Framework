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

    // Client-side Discord rich presence API (Framework.Discord); see types/framework/discord.d.ts.
    // Setters stage onto a pending activity; update()/setPresence() publish it (full-object replace,
    // rate-limited, so callers batch then commit once).
    class Discord final {
      public:
        static void Register(v8::Isolate *isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Object> frameworkObj,
                             Framework::Scripting::ResourceManager *resourceManager);

      private:
        // Data-driven dispatchers; the target field is carried in args.Data().
        static void SetStringFieldCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetNumberFieldCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        static void SetTypeCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetPartySizeCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetPartyPrivacyCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetInstanceCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetAssetsCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetPartyCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetSecretsCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void SetPresenceCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        static void UpdateCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ClearCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ResetCallback(const v8::FunctionCallbackInfo<v8::Value> &args);

        static void GetUserIdCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void IsAvailableCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
    };

} // namespace Framework::Integrations::Client::Scripting::Builtins
