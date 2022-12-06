/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// This file and its CPP code were implemented thanks to the work made by the Alt:MP team
// Some parts were taken from https://github.com/altmp/v8-helpers

#pragma once

#include <function2.hpp>
#include <string>
#include <v8.h>

namespace Framework::Scripting::Helpers {
    bool TryCatch(const fu2::function<bool() const> &, v8::Isolate *isolate = nullptr, v8::Local<v8::Context> context = {});
}
