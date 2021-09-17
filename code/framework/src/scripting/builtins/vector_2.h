#pragma once

#include "../keys.h"
#include "../resource.h"
#include "../v8_helpers/helpers.h"
#include "../v8_helpers/v8_class.h"
#include "../v8_helpers/v8_module.h"

#include <glm/glm.hpp>
#include <iomanip>
#include <sstream>

namespace Framework::Scripting::Builtins {
    static void Vector2Constructor(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        if (!info.IsConstructCall()) {
            V8Helpers::Throw(isolate, "Function cannot be called without new keyword");
            return;
        }

        v8::Local<v8::Object> _this = info.This();
        V8Helpers::ArgumentStack stack(info);

        double x, y;
        if (!V8Helpers::GetVec2(ctx, stack, x, y)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "x", v8::Number::New(isolate, x), v8::PropertyAttribute::ReadOnly);
        V8Helpers::DefineOwnProperty(isolate, ctx, _this, "y", v8::Number::New(isolate, y), v8::PropertyAttribute::ReadOnly);
    }

    static void Vector2Add(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

        double newX, newY;
        if (!V8Helpers::GetVec2(ctx, stack, newX, newY)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        // Acquire old values
        double x, y;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        oldVec += newVec;
        info.GetReturnValue().Set(resource->GetSDK()->CreateVector2(ctx, oldVec));
    }

    static void Vector2Sub(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

        double newX, newY;
        if (!V8Helpers::GetVec2(ctx, stack, newX, newY)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        // Acquire old values
        double x, y;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        oldVec -= newVec;
        info.GetReturnValue().Set(resource->GetSDK()->CreateVector2(ctx, oldVec));
    }

    static void Vector2Mul(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

        double newX, newY;
        if (!V8Helpers::GetVec2(ctx, stack, newX, newY)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        // Acquire old values
        double x, y;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        oldVec *= newVec;
        info.GetReturnValue().Set(resource->GetSDK()->CreateVector2(ctx, oldVec));
    }

    static void Vector2Div(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

        double newX, newY;
        if (!V8Helpers::GetVec2(ctx, stack, newX, newY)) {
            V8Helpers::Throw(isolate, "Argument must be a Vector2 or an array of 2 numbers");
            return;
        }

        // Acquire old values
        double x, y;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        oldVec /= newVec;
        info.GetReturnValue().Set(resource->GetSDK()->CreateVector2(ctx, oldVec));
    }

    static void Vector2Lerp(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        if (info.Length() != 4) {
            V8Helpers::Throw(isolate, "Argument must be an array of 4 floating number");
            return;
        }

        if (!info[0]->IsNumber() || !info[1]->IsNumber() || !info[2]->IsNumber() || !info[3]->IsNumber()) {
            V8Helpers::Throw(isolate, "Every arguments have to be number");
            return;
        }

        auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0));

        v8::Local<v8::Object> _this = info.This();

        // Acquire new values
        V8Helpers::ArgumentStack stack(info);

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
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);

        // Construct our objects
        glm::vec2 oldVec(x, y);
        glm::vec2 newVec(newX, newY);
        info.GetReturnValue().Set(resource->GetSDK()->CreateVector2(ctx, glm::mix(oldVec, newVec, static_cast<float>(f))));
    }

    static void Vector2Length(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        v8::Local<v8::Object> _this = info.This();

        double x, y;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);

        glm::vec2 nativeVec(x, y);
        info.GetReturnValue().Set(static_cast<double>(glm::length(nativeVec)));
    }

    static void Vector2ToArray(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        v8::Local<v8::Object> _this = info.This();

        double x, y, z;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);

        v8::Local<v8::Array> arr = v8::Array::New(isolate, 2);
        arr->Set(ctx, 0, v8::Number::New(isolate, x));
        arr->Set(ctx, 1, v8::Number::New(isolate, y));
        info.GetReturnValue().Set(arr);
    }

    static void Vector2ToString(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate       = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext();

        v8::Local<v8::Object> _this = info.This();

        double x, y, z;
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "x"), ctx, x);
        V8Helpers::SafeToNumber(V8Helpers::Get(ctx, _this, "y"), ctx, y);

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << "Vector2{ x: " << x << ", y: " << y << " }";
        info.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, (ss.str().c_str()), v8::NewStringType::kNormal).ToLocalChecked());
    }

    static void Vector2Register(Scripting::Helpers::V8Module *rootModule) {
        if (!rootModule) {
            return;
        }

        auto vec3Class = new Helpers::V8Class(GetKeyName(Keys::KEY_VECTOR_2), Vector2Constructor, [](v8::Local<v8::FunctionTemplate> tpl) {
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

        rootModule->AddClass(vec3Class);
    }
} // namespace Framework::Scripting::Builtins
