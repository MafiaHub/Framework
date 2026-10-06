/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/node_runtime.h"

#include <string>

namespace NodeTest {
    // Runs `source` in the runtime and returns the result as a string, or "<threw> " and the exception.
    static std::string Eval(Framework::Scripting::NodeRuntime &runtime, const std::string &source) {
        v8::Isolate *isolate = runtime.GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = runtime.GetContext();
        v8::Context::Scope contextScope(context);
        v8::TryCatch tryCatch(isolate);

        v8::Local<v8::Script> script;
        v8::Local<v8::Value> result;
        if (!v8::Script::Compile(context, v8::String::NewFromUtf8(isolate, source.c_str()).ToLocalChecked()).ToLocal(&script) || !script->Run(context).ToLocal(&result)) {
            v8::String::Utf8Value text(isolate, tryCatch.Exception());
            return std::string("<threw> ") + (*text ? *text : "");
        }
        v8::String::Utf8Value text(isolate, result);
        return *text ? *text : "";
    }
} // namespace NodeTest
