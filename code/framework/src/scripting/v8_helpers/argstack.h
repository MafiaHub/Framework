/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <queue>
#include <v8.h>

namespace Framework::Scripting::V8Helpers {
    class ArgumentStack final {
      private:
        std::queue<v8::Local<v8::Value>> _args;
        v8::Isolate *_isolate;

      public:
        ArgumentStack(const v8::FunctionCallbackInfo<v8::Value> &info);

        v8::Local<v8::Value> Pop();

        v8::Local<v8::Value> Peek();

        bool IsEmpty() const {
            return _args.empty();
        }
    };
} // namespace Framework::Scripting::V8Helpers
