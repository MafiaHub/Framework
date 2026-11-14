/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "v8pp_glm.h"

#include "builtins/quaternion.h"
#include "builtins/vector2.h"
#include "builtins/vector3.h"
#include "builtins/vector4.h"

namespace v8pp {
    v8::Local<v8::Object> convert<glm::vec2>::to_v8(v8::Isolate *isolate, const glm::vec2 &value) {
        return Framework::Scripting::Builtins::Vector2::NewInstance(isolate, value);
    }

    v8::Local<v8::Object> convert<glm::vec3>::to_v8(v8::Isolate *isolate, const glm::vec3 &value) {
        return Framework::Scripting::Builtins::Vector3::NewInstance(isolate, value);
    }

    v8::Local<v8::Object> convert<glm::vec4>::to_v8(v8::Isolate *isolate, const glm::vec4 &value) {
        return Framework::Scripting::Builtins::Vector4::NewInstance(isolate, value);
    }

    v8::Local<v8::Object> convert<glm::quat>::to_v8(v8::Isolate *isolate, const glm::quat &value) {
        return Framework::Scripting::Builtins::Quaternion::NewInstance(isolate, value);
    }
} // namespace v8pp
