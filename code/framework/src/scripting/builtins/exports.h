/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <v8.h>
#include <v8pp/convert.hpp>

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Framework::Scripting::Builtins {

    class Exports final {
      public:
        static void Register(v8::Isolate *isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Object> frameworkObj,
                            ResourceManager *resourceManager);

      private:
        static void RegisterCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void GetCallback(const v8::FunctionCallbackInfo<v8::Value> &args);
    };

} // namespace Framework::Scripting::Builtins
