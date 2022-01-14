/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../v8_helpers/v8_module.h"

#include <glm/ext.hpp>
#include <v8.h>

namespace Framework::Scripting::Builtins {
    v8::Local<v8::Value> CreateVector2(Helpers::V8Module *module, v8::Local<v8::Context>, glm::vec2);
    v8::Local<v8::Value> CreateVector3(Helpers::V8Module *module, v8::Local<v8::Context>, glm::vec3);
    v8::Local<v8::Value> CreateQuaternion(Helpers::V8Module *module, v8::Local<v8::Context>, glm::quat);
} // namespace Framework::Scripting::Builtins
