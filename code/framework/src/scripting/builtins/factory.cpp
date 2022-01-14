/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "factory.h"

#include "../keys.h"
#include "../v8_helpers/v8_class.h"

#include <string>
#include <vector>

namespace Framework::Scripting::Builtins {
    v8::Local<v8::Value> CreateVector2(Helpers::V8Module *module, v8::Local<v8::Context> ctx, glm::vec2 v) {
        auto isolate = v8::Isolate::GetCurrent();
        std::vector<v8::Local<v8::Value>> args {v8::Number::New(isolate, v.x), v8::Number::New(isolate, v.y)};
        Helpers::V8Class *cls = module->GetClass(GetKeyName(Keys::KEY_VECTOR_2));
        return cls->CreateInstance(isolate, ctx, args);
    }

    v8::Local<v8::Value> CreateVector3(Helpers::V8Module *module, v8::Local<v8::Context> ctx, glm::vec3 v) {
        auto isolate = v8::Isolate::GetCurrent();
        std::vector<v8::Local<v8::Value>> args {v8::Number::New(isolate, v.x), v8::Number::New(isolate, v.y), v8::Number::New(isolate, v.z)};
        Helpers::V8Class *cls = module->GetClass(GetKeyName(Keys::KEY_VECTOR_3));
        return cls->CreateInstance(isolate, ctx, args);
    }

    v8::Local<v8::Value> CreateQuaternion(Helpers::V8Module *module, v8::Local<v8::Context> ctx, glm::quat q) {
        auto isolate = v8::Isolate::GetCurrent();
        std::vector<v8::Local<v8::Value>> args {v8::Number::New(isolate, q.w), v8::Number::New(isolate, q.x), v8::Number::New(isolate, q.y), v8::Number::New(isolate, q.z)};
        Helpers::V8Class *cls = module->GetClass(GetKeyName(Keys::KEY_QUATERNION));
        return cls->CreateInstance(isolate, ctx, args);
    }
} // namespace Framework::Scripting::Builtins
