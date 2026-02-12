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
