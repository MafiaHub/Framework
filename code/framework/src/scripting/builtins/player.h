#pragma once

#include "../errors.h"
#include "../resource.h"
#include "../v8_helpers/v8_string.h"

#include <v8.h>

namespace Framework::Scripting::Builtins {
    static void PlayerGetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().SetNull();
    }

    static void PlayerGetAll(v8::Local<v8::String> name, const v8::PropertyCallbackInfo<v8::Value> &info) {
        v8::Local<v8::Value> emptyArray = {};
        info.GetReturnValue().Set(emptyArray);
    }

    static void PlayerGetName(v8::Local<v8::String> name, const v8::PropertyCallbackInfo<v8::Value> &info) {
        auto isolate = v8::Isolate::GetCurrent();
        info.GetReturnValue().Set(
            v8::String::NewFromUtf8(isolate, "john_doe", v8::NewStringType::kNormal).ToLocalChecked());
    }
} // namespace Framework::Scripting::Builtins
