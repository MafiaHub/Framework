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
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        if (!info.IsConstructCall()) {
            V8Helpers::Throw(isolate, "Function cannot be called without new keyword");
            return;
        }

        v8::Local<v8::Object> _this = info.This();
        V8Helpers::ArgumentStack stack(info);

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
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

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
        info.GetReturnValue().Set(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector3Sub(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

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
        info.GetReturnValue().Set(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector3Mul(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

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
        info.GetReturnValue().Set(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector3Div(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

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
        info.GetReturnValue().Set(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, oldVec));
    }

    static void Vector3Lerp(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

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
        info.GetReturnValue().Set(CreateVector3(resource->GetSDK()->GetRootModule(), ctx, glm::mix(oldVec, newVec, static_cast<float>(f))));
    }

    static void Vector3Length(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        v8::Local<v8::Object> _this = info.This();

        double x, y, z;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "z"), ctx, z);

        glm::vec3 nativeVec(x, y, z);
        info.GetReturnValue().Set(static_cast<double>(glm::length(nativeVec)));
    }

    static void Vector3ToArray(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        v8::Local<v8::Object> _this = info.This();

        double x, y, z;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "z"), ctx, z);

        v8::Local<v8::Array> arr = v8::Array::New(isolate, 3);
        arr->Set(ctx, 0, v8::Number::New(isolate, x));
        arr->Set(ctx, 1, v8::Number::New(isolate, y));
        arr->Set(ctx, 2, v8::Number::New(isolate, z));
        info.GetReturnValue().Set(arr);
    }

    static void Vector3ToString(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        v8::Local<v8::Object> _this = info.This();

        double x, y, z;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "z"), ctx, z);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << "Vector3{ x: " << x << ", y: " << y << ", z: " << z << " }";
        info.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, (ss.str().c_str()), v8::NewStringType::kNormal).ToLocalChecked());
    }

    static void Vector3Register(Scripting::Helpers::V8Module *rootModule) {
        if (!rootModule) {
            return;
        }

        auto vec3Class = new Helpers::V8Class(GetKeyName(Keys::KEY_VECTOR_3), Vector3Constructor, [](v8::Local<v8::FunctionTemplate> tpl) {
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
