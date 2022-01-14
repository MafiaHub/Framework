/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../errors.h"
#include "../resource.h"
#include "../v8_helpers/helpers.h"
#include "../v8_helpers/v8_string.h"

#include <v8.h>

namespace Framework::Scripting::Builtins {
    static void PlayerGetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_RETURN_NULL();
    }

    static void PlayerGetAll(v8::Local<v8::String> name, const v8::PropertyCallbackInfo<v8::Value> &info) {
        v8::Local<v8::Value> emptyArray = {};
        V8_RETURN(emptyArray);
    }

    static void PlayerGetName(v8::Local<v8::String> name, const v8::PropertyCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE();
        V8_RETURN(v8::String::NewFromUtf8(isolate, "john_doe", v8::NewStringType::kNormal).ToLocalChecked());
    }
} // namespace Framework::Scripting::Builtins
