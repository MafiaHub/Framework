// This file and its CPP code were implemented thanks to the work made by the Alt:MP team
// Some parts were taken from https://github.com/altmp/v8-helpers

#pragma once

#include <functional>
#include <string>
#include <v8.h>

namespace Framework::Scripting::Helpers {
    bool TryCatch(const std::function<bool()> &, v8::Isolate *isolate = nullptr, v8::Local<v8::Context> context = {});
}
