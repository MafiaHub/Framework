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
    inline void Vector2Extract(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, double &x, double &y) {
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, obj, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, obj, "y"), ctx, y);
    }

    static void Vector2Constructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_VALIDATE_CTOR_CALL();

        V8_GET_SELF();
        V8_DEFINE_STACK();

        double x, y;
        if (!V8Helpers::GetVec2(ctx, stack, x, y)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "x", v8::Number::New(isolate, x), v8::PropertyAttribute::ReadOnly);
        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "y", v8::Number::New(isolate, y), v8::PropertyAttribute::ReadOnly);
    }

    static void Vector2Add(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY;
        if (!V8Helpers::GetVec2(ctx, stack, newX, newY)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        // Acquire old values
        double x, y;
        Vector2Extract(ctx, _this, x, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        oldVec += newVec;
        V8_RETURN(CreateVector2(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector2Sub(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY;
        if (!V8Helpers::GetVec2(ctx, stack, newX, newY)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        // Acquire old values
        double x, y;
        Vector2Extract(ctx, _this, x, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        oldVec -= newVec;
        V8_RETURN(CreateVector2(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector2Mul(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY;
        if (!V8Helpers::GetVec2(ctx, stack, newX, newY)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        // Acquire old values
        double x, y;
        Vector2Extract(ctx, _this, x, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        oldVec *= newVec;
        V8_RETURN(CreateVector2(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector2Div(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY;
        if (!V8Helpers::GetVec2(ctx, stack, newX, newY)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        // Acquire old values
        double x, y;
        Vector2Extract(ctx, _this, x, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        oldVec /= newVec;
        V8_RETURN(CreateVector2(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector2Lerp(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_SUITE();

        V8_GET_SELF();

        // Acquire new values
        V8_DEFINE_STACK();

        double newX, newY;
        if (!V8Helpers::GetVec2(ctx, stack, newX, newY)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        // Acquire factor
        double f;
        V8Helpers::SafeToNumber(stack.Pop(), ctx, f);

        // Acquire old values
        double x, y;
        Vector2Extract(ctx, _this, x, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        V8_RETURN(CreateVector2(resource->GetSDK()->GetRootModule(), ctx, glm::mix(oldVec, newVec, static_cast<float>(f))));
    }

    static void Vector2Length(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        V8_GET_SELF();

        double x, y;
        Vector2Extract(ctx, _this, x, y);

        glm::vec2 nativeVec(x, y);
        V8_RETURN(static_cast<double>(glm::length(nativeVec)));
    }

    static void Vector2ToArray(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        double x, y;
        Vector2Extract(ctx, _this, x, y);

        v8::Local<v8::Array> arr = v8::Array::New(isolate, 2);
        arr->Set(ctx, 0, v8::Number::New(isolate, x));
        arr->Set(ctx, 1, v8::Number::New(isolate, y));
        V8_RETURN(arr);
    }

    static void Vector2ToString(const v8::FunctionCallbackInfo<v8::Value> &info) {
        V8_GET_ISOLATE_CONTEXT();

        V8_GET_SELF();

        double x, y;
        Vector2Extract(ctx, _this, x, y);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << "Vector2{ x: " << x << ", y: " << y << " }";
        V8_RETURN(v8::String::NewFromUtf8(isolate, (ss.str().c_str()), v8::NewStringType::kNormal).ToLocalChecked());
    }

    static void Vector2Register(Scripting::Helpers::V8Module *rootModule) {
        if (!rootModule) {
            return;
        }

        auto vec2Class = new Helpers::V8Class(
            GetKeyName(Keys::KEY_VECTOR_2), Vector2Constructor, V8_CLASS_CB {
                v8::Isolate *isolate = v8::Isolate::GetCurrent();
                V8Helpers::SetAccessor(isolate, tpl, "length", Vector2Length);

                V8Helpers::SetMethod(isolate, tpl, "add", Vector2Add);
                V8Helpers::SetMethod(isolate, tpl, "sub", Vector2Sub);
                V8Helpers::SetMethod(isolate, tpl, "mul", Vector2Mul);
                V8Helpers::SetMethod(isolate, tpl, "div", Vector2Div);
                V8Helpers::SetMethod(isolate, tpl, "lerp", Vector2Lerp);
                V8Helpers::SetMethod(isolate, tpl, "toArray", Vector2ToArray);
                V8Helpers::SetMethod(isolate, tpl, "toString", Vector2ToString);
            });

        rootModule->AddClass(vec2Class);
    }
} // namespace Framework::Scripting::Builtins
