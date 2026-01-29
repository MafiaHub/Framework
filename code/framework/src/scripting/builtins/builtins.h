#pragma once

#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "quaternion.h"
#include "color.h"

#include <v8.h>

namespace Framework::Scripting::Builtins {

    /**
     * Register all builtin types to the global object.
     */
    inline void RegisterAll(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        Vector2::Register(isolate, global);
        Vector3::Register(isolate, global);
        Vector4::Register(isolate, global);
        Quaternion::Register(isolate, global);
        Color::Register(isolate, global);
    }

} // namespace Framework::Scripting::Builtins
