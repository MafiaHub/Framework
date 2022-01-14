/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// This file and its CPP code were implemented thanks to the work made by the Alt:MP team
// Some parts were taken from https://github.com/altmp/v8-helpers

#pragma once

#include <string>
#include <v8.h>

namespace Framework::Scripting::Helpers {
    inline const char *ToCString(v8::Local<v8::String> str) {
        v8::String::Utf8Value value(v8::Isolate::GetCurrent(), str);
        return *value ? *value : "<string conversion failed>";
    }

    inline const std::string ToString(v8::Local<v8::String> str) {
        v8::String::Utf8Value value(v8::Isolate::GetCurrent(), str);
        return *value ? *value : "<string conversion failed>";
    }

    inline v8::MaybeLocal<v8::String> MakeString(v8::Isolate *isolate, const char *str) {
        return v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kNormal);
    }

    inline v8::MaybeLocal<v8::String> MakeString(v8::Isolate *isolate, const std::string &str) {
        return MakeString(isolate, str.c_str());
    }
} // namespace Framework::Scripting::Helpers
