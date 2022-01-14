/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "argstack.h"

namespace Framework::Scripting::V8Helpers {
    ArgumentStack::ArgumentStack(const v8::FunctionCallbackInfo<v8::Value> &info) {
        _isolate = info.GetIsolate();
        for (size_t i = 0; i < info.Length(); i++) { _args.push(info[i]); }
    }

    v8::Local<v8::Value> ArgumentStack::Pop() {
        if (IsEmpty())
            return v8::Null(_isolate);
        auto local = Peek();
        _args.pop();
        return local;
    }

    v8::Local<v8::Value> ArgumentStack::Peek() {
        if (IsEmpty())
            return v8::Null(_isolate);
        return _args.front();
    }
} // namespace Framework::Scripting::V8Helpers
