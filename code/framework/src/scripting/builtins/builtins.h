/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "quaternion.h"
#include "color.h"

#include <v8.h>

namespace Framework::Scripting::Builtins {

    /**
     * Register all builtin types on the target object.
     */
    inline void RegisterAll(v8::Isolate *isolate, v8::Local<v8::Object> target) {
        Vector2::Register(isolate, target);
        Vector3::Register(isolate, target);
        Vector4::Register(isolate, target);
        Quaternion::Register(isolate, target);
        Color::Register(isolate, target);
    }

} // namespace Framework::Scripting::Builtins
