/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// This file has been implemented thanks to the work made by the Alt:MP team
// Some parts were taken from https://github.com/altmp/v8-helpers

#pragma once

#include "../keys.h"
#include "argstack.h"
#include "v8_string.h"

#include <v8.h>

namespace Framework::Scripting::V8Helpers {
    inline void Throw(v8::Isolate *isolate, const std::string &msg) {
        isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, msg.data(), v8::NewStringType::kNormal, msg.size()).ToLocalChecked()));
    }

    inline void SetStaticMethod(v8::Isolate *isolate, v8::Local<v8::FunctionTemplate> tpl, const char *name, v8::FunctionCallback callback) {
        tpl->Set(isolate, name, v8::FunctionTemplate::New(isolate, callback));
    }

    inline void SetAccessor(
        v8::Isolate *isolate, v8::Local<v8::FunctionTemplate> tpl, const char *name, v8::AccessorGetterCallback getter, v8::AccessorSetterCallback setter = nullptr) {
        tpl->PrototypeTemplate()->SetAccessor(v8::String::NewFromUtf8(isolate, name, v8::NewStringType::kInternalized).ToLocalChecked(), getter, setter, v8::Local<v8::Value>(),
            v8::AccessControl::DEFAULT, setter != nullptr ? v8::PropertyAttribute::None : v8::PropertyAttribute::ReadOnly);
    }

    inline void SetStaticAccessor(
        v8::Isolate *isolate, v8::Local<v8::FunctionTemplate> tpl, const char *name, v8::AccessorGetterCallback getter, v8::AccessorSetterCallback setter = nullptr) {
        tpl->SetNativeDataProperty(v8::String::NewFromUtf8(isolate, name, v8::NewStringType::kInternalized).ToLocalChecked(), getter, setter);
    }

    inline void RegisterFunc(v8::Local<v8::Context> ctx, v8::Local<v8::Object> exports, const std::string &_name, v8::FunctionCallback cb, void *data = nullptr) {
        v8::Isolate *isolate = v8::Isolate::GetCurrent();

        v8::Local<v8::String> name = v8::String::NewFromUtf8(isolate, _name.data(), v8::NewStringType::kNormal, _name.size()).ToLocalChecked();

        v8::Local<v8::Function> fn = v8::Function::New(ctx, cb, v8::External::New(isolate, data)).ToLocalChecked();
        fn->SetName(name);

        v8::Maybe<bool> res = exports->Set(ctx, name, fn);
        if (!res.ToChecked()) {
            return;
        }
    }
    inline void RegisterProperty(v8::Local<v8::Context> ctx, v8::Local<v8::Object> exports, const std::string &_name, v8::AccessorNameGetterCallback getter,
        v8::AccessorNameSetterCallback setter = nullptr, void *data = nullptr) {
        v8::Isolate *isolate = v8::Isolate::GetCurrent();

        exports->SetNativeDataProperty(
            ctx, v8::String::NewFromUtf8(isolate, _name.c_str(), v8::NewStringType::kInternalized).ToLocalChecked(), getter, setter, v8::External::New(isolate, data));
    }

    inline void DefineOwnProperty(v8::Isolate *isolate, v8::Local<v8::Context> ctx, v8::Local<v8::Object> val, const char *name, v8::Local<v8::Value> value,
        v8::PropertyAttribute attributes = v8::PropertyAttribute::None) {
        val->DefineOwnProperty(ctx, v8::String::NewFromUtf8(isolate, name, v8::NewStringType::kInternalized).ToLocalChecked(), value, attributes);
    }

    inline void DefineOwnProperty(v8::Isolate *isolate, v8::Local<v8::Context> ctx, v8::Local<v8::Object> val, v8::Local<v8::String> name, v8::Local<v8::Value> value,
        v8::PropertyAttribute attributes = v8::PropertyAttribute::None) {
        val->DefineOwnProperty(ctx, name, value, attributes);
    }

    inline void SetMethod(v8::Isolate *isolate, v8::Local<v8::FunctionTemplate> tpl, const char *name, v8::FunctionCallback callback) {
        tpl->PrototypeTemplate()->Set(isolate, name, v8::FunctionTemplate::New(isolate, callback));
    }

    inline v8::Local<v8::Value> Get(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, const char *name) {
        return obj->Get(ctx, v8::String::NewFromUtf8(ctx->GetIsolate(), name, v8::NewStringType::kInternalized).ToLocalChecked()).ToLocalChecked();
    }

    inline v8::Local<v8::Value> Get(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, v8::Local<v8::Name> name) {
        return obj->Get(ctx, name).ToLocalChecked();
    }

    inline void Set(v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj, v8::Local<v8::Name> name, v8::Local<v8::Value> value) {
        obj->Set(ctx, name, value);
    }

    inline void Set(v8::Local<v8::Context> ctx, v8::Isolate *isolate, v8::Local<v8::Object> obj, const char *name, v8::Local<v8::Value> value) {
        Set(ctx, obj, Helpers::MakeString(isolate, name).ToLocalChecked(), value);
    }

    inline bool SafeToNumber(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, double &out) {
        v8::MaybeLocal maybeVal = val->ToNumber(ctx);
        if (maybeVal.IsEmpty())
            return false;
        out = maybeVal.ToLocalChecked()->Value();
        return true;
    }

    inline bool SafeToInteger(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, int32_t &out) {
        v8::MaybeLocal maybeVal = val->ToInteger(ctx);
        if (maybeVal.IsEmpty())
            return false;
        out = maybeVal.ToLocalChecked()->Value();
        return true;
    }

    inline bool SafeToInteger(v8::Local<v8::Value> val, v8::Local<v8::Context> ctx, int64_t &out) {
        v8::MaybeLocal maybeVal = val->ToBigInt(ctx);
        if (maybeVal.IsEmpty())
            return false;
        out = maybeVal.ToLocalChecked()->Int64Value();
        return true;
    }

    inline bool GetVec3(v8::Local<v8::Context> ctx, ArgumentStack &stack, double &x, double &y, double &z) {
        v8::Local<v8::Value> front = stack.Peek();
        if (front->IsObject() && Helpers::ToString(front.As<v8::Object>()->GetConstructorName()) == GetKeyName(Keys::KEY_VECTOR_3)) {
            auto arg = stack.Pop();
            if (!arg->IsObject()) {
                return false;
            }

            v8::Local<v8::Object> vec3 = arg.As<v8::Object>();
            bool r1                    = SafeToNumber(Get(ctx, vec3, "x"), ctx, x);
            bool r2                    = SafeToNumber(Get(ctx, vec3, "y"), ctx, y);
            bool r3                    = SafeToNumber(Get(ctx, vec3, "z"), ctx, z);
            return r1 && r2 && r3;
        }
        else {
            bool r1 = SafeToNumber(stack.Pop(), ctx, x);
            bool r2 = SafeToNumber(stack.Pop(), ctx, y);
            bool r3 = SafeToNumber(stack.Pop(), ctx, z);
            return r1 && r2 && r3;
        }
        return false;
    }

    inline bool GetVec2(v8::Local<v8::Context> ctx, ArgumentStack &stack, double &x, double &y) {
        v8::Local<v8::Value> front = stack.Peek();
        if (front->IsObject() && Helpers::ToString(front.As<v8::Object>()->GetConstructorName()) == GetKeyName(Keys::KEY_VECTOR_2)) {
            auto arg = stack.Pop();
            if (!arg->IsObject()) {
                return false;
            }

            v8::Local<v8::Object> vec2 = arg.As<v8::Object>();
            bool r1                    = SafeToNumber(Get(ctx, vec2, "x"), ctx, x);
            bool r2                    = SafeToNumber(Get(ctx, vec2, "y"), ctx, y);
            return r1 && r2;
        }
        else {
            bool r1 = SafeToNumber(stack.Pop(), ctx, x);
            bool r2 = SafeToNumber(stack.Pop(), ctx, y);
            return r1 && r2;
        }
        return false;
    }

    inline bool GetQuat(v8::Local<v8::Context> ctx, ArgumentStack &stack, double &w, double &x, double &y, double &z) {
        v8::Local<v8::Value> front = stack.Peek();
        if (front->IsObject() && Helpers::ToString(front.As<v8::Object>()->GetConstructorName()) == GetKeyName(Keys::KEY_QUATERNION)) {
            auto arg = stack.Pop();
            if (!arg->IsObject()) {
                return false;
            }

            v8::Local<v8::Object> quat = arg.As<v8::Object>();
            bool r1                    = SafeToNumber(Get(ctx, quat, "w"), ctx, w);
            bool r2                    = SafeToNumber(Get(ctx, quat, "x"), ctx, x);
            bool r3                    = SafeToNumber(Get(ctx, quat, "y"), ctx, y);
            bool r4                    = SafeToNumber(Get(ctx, quat, "z"), ctx, z);
            return r1 && r2 && r3 && r4;
        }
        else {
            bool r1 = SafeToNumber(stack.Pop(), ctx, w);
            bool r2 = SafeToNumber(stack.Pop(), ctx, x);
            bool r3 = SafeToNumber(stack.Pop(), ctx, y);
            bool r4 = SafeToNumber(stack.Pop(), ctx, z);
            return r1 && r2 && r3 && r4;
        }
        return false;
    }

    inline v8::Local<v8::String> JSValue(const char *val) {
        return v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), val).ToLocalChecked();
    }
    inline v8::Local<v8::String> JSValue(const std::string &val) {
        return v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), val.c_str(), v8::NewStringType::kNormal, (int)val.size()).ToLocalChecked();
    }
    inline v8::Local<v8::String> JSValue(const uint16_t *val) {
        return v8::String::NewFromTwoByte(v8::Isolate::GetCurrent(), val).ToLocalChecked();
    }
    inline v8::Local<v8::String> JSValue(const std::wstring &val) {
        return v8::String::NewFromTwoByte(v8::Isolate::GetCurrent(), (uint16_t *)val.data()).ToLocalChecked();
    }
    inline v8::Local<v8::Boolean> JSValue(bool val) {
        return v8::Boolean::New(v8::Isolate::GetCurrent(), val);
    }
    inline v8::Local<v8::Number> JSValue(double val) {
        return v8::Number::New(v8::Isolate::GetCurrent(), val);
    }
    inline v8::Local<v8::Integer> JSValue(int32_t val) {
        return v8::Integer::New(v8::Isolate::GetCurrent(), val);
    }
    inline v8::Local<v8::Integer> JSValue(uint32_t val) {
        return v8::Integer::NewFromUnsigned(v8::Isolate::GetCurrent(), val);
    }
    inline v8::Local<v8::BigInt> JSValue(int64_t val) {
        return v8::BigInt::New(v8::Isolate::GetCurrent(), val);
    }
    inline v8::Local<v8::BigInt> JSValue(uint64_t val) {
        return v8::BigInt::NewFromUnsigned(v8::Isolate::GetCurrent(), val);
    }
    template <class T>
    inline v8::Local<v8::Array> JSValue(std::vector<T> &arr) {
        auto jsArr = v8::Array::New(v8::Isolate::GetCurrent(), arr.size());
        for (int i = 0; i < arr.size(); i++) { jsArr->Set(v8::Isolate::GetCurrent()->GetEnteredOrMicrotaskContext(), i, JSValue(arr[i])); }
        return jsArr;
    }
    // Returns null
    inline v8::Local<v8::Primitive> JSValue(std::nullptr_t val) {
        return v8::Null(v8::Isolate::GetCurrent());
    }

    inline std::string GetCurrentSourceOrigin(v8::Isolate *isolate) {
        auto stackTrace = v8::StackTrace::CurrentStackTrace(isolate, 1);
        if (stackTrace->GetFrameCount() == 0)
            return "";
        return *v8::String::Utf8Value(isolate, stackTrace->GetFrame(isolate, 0)->GetScriptName());
    }
} // namespace Framework::Scripting::V8Helpers

#define V8_GET_ISOLATE() v8::Isolate *isolate = info.GetIsolate()
#define V8_GET_CONTEXT() v8::Local<v8::Context> ctx = isolate->GetEnteredOrMicrotaskContext()
#define V8_GET_ISOLATE_CONTEXT()                                                                                                                                                   \
    V8_GET_ISOLATE();                                                                                                                                                              \
    V8_GET_CONTEXT()

#define V8_GET_RESOURCE() auto resource = static_cast<Resource *>(ctx->GetAlignedPointerFromEmbedderData(0))

#define V8_GET_SUITE()                                                                                                                                                             \
    V8_GET_ISOLATE();                                                                                                                                                              \
    V8_GET_CONTEXT();                                                                                                                                                              \
    V8_GET_RESOURCE()

#define V8_RETURN(ret) info.GetReturnValue().Set(ret)

#define V8_RETURN_NULL() info.GetReturnValue().SetNull()

#define V8_GET_SELF() v8::Local<v8::Object> _this = info.This()

#define V8_DEFINE_STACK() V8Helpers::ArgumentStack stack(info)

#define V8_VALIDATE_CTOR_CALL()                                                                                                                                                    \
    if (!info.IsConstructCall()) {                                                                                                                                                 \
        V8Helpers::Throw(isolate, "Function cannot be called without new keyword");                                                                                                \
        return;                                                                                                                                                                    \
    }

#define V8_MODULE_CB [](v8::Local<v8::Context> ctx, v8::Local<v8::Object> obj)
#define V8_CLASS_CB  [](v8::Local<v8::FunctionTemplate> tpl)
#define V8_METHOD_CB [](const v8::FunctionCallbackInfo<v8::Value> &info)

#define V8_EVENT_ARGS std::vector<v8::Local<v8::Value>>
#define V8_EVENT_CB   [=](v8::Isolate * isolate, v8::Local<v8::Context> ctx) -> std::vector<v8::Local<v8::Value>>
