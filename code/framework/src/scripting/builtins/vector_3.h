/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../keys.h"
#include "../resource.h"
#include "../v8_helpers/helpers.h"
#include "../v8_helpers/v8_class.h"
#include "../v8_helpers/v8_module.h"
#include "factory.h"

#include <glm/glm.hpp>
#include <iomanip>
#include <sstream>

namespace Framework::Scripting::Builtins {
    inline void Vector3Extract(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, double &x, double &y, double &z) {
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, obj, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, obj, "y"), ctx, y);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, obj, "z"), ctx, z);
    }

    static void Vector3Constructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_VALIDATE_CTOR_CALL();

        V8_GET_SELF();
        V8_DEFINE_STACK();

        double x, y, z;
        if (!V8Helpers::GetVec3(ctx, stack, x, y, z)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "x", v8::Number::New(isolate, x), v8::PropertyAttribute::ReadOnly);
        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "y", v8::Number::New(isolate, y), v8::PropertyAttribute::ReadOnly);
        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "z", v8::Number::New(isolate, z), v8::PropertyAttribute::ReadOnly);
    }

    static void Vector3Add(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY, newZ;
        if (!V8Helpers::GetVec3(ctx, stack, newX, newY, newZ)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        // Acquire old values
        double x, y, z;
        Vector3Extract(ctx, _this, x, y, z);

        // Construct our objects
        glm::vec3 oldVec(x, y, z);
        glm::vec3 newVec(newX, newY, newZ);
        oldVec += newVec;
        V8_RETURN(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector3Sub(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY, newZ;
        if (!V8Helpers::GetVec3(ctx, stack, newX, newY, newZ)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        // Acquire old values
        double x, y, z;
        Vector3Extract(ctx, _this, x, y, z);

        // Construct our objects
        glm::vec3 oldVec(x, y, z);
        glm::vec3 newVec(newX, newY, newZ);
        oldVec -= newVec;
        V8_RETURN(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector3Mul(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY, newZ;
        if (!V8Helpers::GetVec3(ctx, stack, newX, newY, newZ)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        // Acquire old values
        double x, y, z;
        Vector3Extract(ctx, _this, x, y, z);

        // Construct our objects
        glm::vec3 oldVec(x, y, z);
        glm::vec3 newVec(newX, newY, newZ);
        oldVec *= newVec;
        V8_RETURN(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector3Div(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY, newZ;
        if (!V8Helpers::GetVec3(ctx, stack, newX, newY, newZ)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        // Acquire old values
        double x, y, z;
        Vector3Extract(ctx, _this, x, y, z);

        // Construct our objects
        glm::vec3 oldVec(x, y, z);
        glm::vec3 newVec(newX, newY, newZ);
        oldVec /= newVec;
        V8_RETURN(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector3Lerp(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY, newZ;
        if (!V8Helpers::GetVec3(ctx, stack, newX, newY, newZ)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector3 or an array of 3 numbers");
            return;
        }

        // Acquire factor
        double f;
        V8Helpers::SafeToNumber(stack.Pop(), ctx, f);

        // Acquire old values
        double x, y, z;
        Vector3Extract(ctx, _this, x, y, z);

        // Construct our objects
        glm::vec3 oldVec(x, y, z);
        glm::vec3 newVec(newX, newY, newZ);
        V8_RETURN(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, glm::mix(oldVec, newVec, static_cast<float>(f))));
    }

    static void Vector3Length(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        double x, y, z;
        Vector3Extract(ctx, _this, x, y, z);

        glm::vec3 nativeVec(x, y, z);
        V8_RETURN(static_cast<double>(glm::length(nativeVec)));
    }

    static void Vector3ToArray(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        double x, y, z;
        Vector3Extract(ctx, _this, x, y, z);

        v8::Local<v8::Array> arr = v8::Array::New(isolate, 3);
        arr->Set(ctx, 0, v8::Number::New(isolate, x));
        arr->Set(ctx, 1, v8::Number::New(isolate, y));
        arr->Set(ctx, 2, v8::Number::New(isolate, z));
        V8_RETURN(arr);
    }

    static void Vector3ToString(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        double x, y, z;
        Vector3Extract(ctx, _this, x, y, z);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << "Vector3{ x: " << x << ", y: " << y << ", z: " << z << " }";
        V8_RETURN(v8::String::NewFromUtf8(isolate, (ss.str().c_str()), v8::NewStringType::kNormal).ToLocalChecked());
    }

    static void Vector3Register(Scripting::Helpers::V8Module *rootModule) {
        if (!rootModule) {
            return;
        }

        auto vec3Class = new Helpers::V8Class(
            GetKeyName(Keys::KEY_VECTOR_3), Vector3Constructor, V8_CLASS_CB {
                v8::Isolate *isolate = v8::Isolate::GetCurrent();
                V8Helpers::SetAccessor(isolate, tpl, "length", Vector3Length);

                V8Helpers::SetMethod(isolate, tpl, "add", Vector3Add);
                V8Helpers::SetMethod(isolate, tpl, "sub", Vector3Sub);
                V8Helpers::SetMethod(isolate, tpl, "mul", Vector3Mul);
                V8Helpers::SetMethod(isolate, tpl, "div", Vector3Div);
                V8Helpers::SetMethod(isolate, tpl, "lerp", Vector3Lerp);
                V8Helpers::SetMethod(isolate, tpl, "toArray", Vector3ToArray);
                V8Helpers::SetMethod(isolate, tpl, "toString", Vector3ToString);
            });

        rootModule->AddClass(vec3Class);
    }
} // namespace Framework::Scripting::Builtins
