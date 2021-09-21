// This file and its CPP code were implemented thanks to the work made by the Alt:MP team
// Some parts were taken from https://github.com/altmp/v8-helpers

#pragma once

#include <string>
#include <v8.h>

namespace Framework::Scripting::Helpers {
    const char *ToCString(v8::Local<v8::String> str) {
        v8::String::Utf8Value value(v8::Isolate::GetCurrent(), str);
        return *value ? *value : "<string conversion failed>";
    }

    const std::string ToString(v8::Local<v8::String> str) {
        v8::String::Utf8Value value(v8::Isolate::GetCurrent(), str);
        return *value ? *value : "<string conversion failed>";
    }
} // namespace Framework::Scripting::Helpers
