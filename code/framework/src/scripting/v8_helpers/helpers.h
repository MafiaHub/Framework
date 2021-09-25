// This file has been implemented thanks to the work made by the Alt:MP team
// Some parts were taken from https://github.com/altmp/v8-helpers

#pragma once
#include "argstack.h"
#include "v8_string.h"
#include "../keys.h"

#include <v8.h>

namespace Framework::Scripting::V8Helpers {
    inline void Throw(v8::Isolate *isolate, const std::string &msg) {
        isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, msg.data(), v8::NewStringType::kNormal, msg.size()).ToLocalChecked()));
    }

    inline void SetStaticMethod(v8::Isolate *isolate, v8::Local<v8::FunctionTemplate> tpl, const char *name, v8::FunctionCallback callback) {
        tpl->Set(isolate, name, v8::FunctionTemplate::New(isolate, callback));
    }

    inline void SetAccessor(v8::Isolate *isolate, v8::Local<v8::FunctionTemplate> tpl, const char *name, v8::AccessorGetterCallback getter, v8::AccessorSetterCallback setter = nullptr) {
        tpl->PrototypeTemplate()->SetAccessor(v8::String::NewFromUtf8(isolate, name, v8::NewStringType::kInternalized).ToLocalChecked(), getter, setter, v8::Local<v8::Value>(),
                                              v8::AccessControl::DEFAULT, setter != nullptr ? v8::PropertyAttribute::None : v8::PropertyAttribute::ReadOnly);
    }

    inline void SetStaticAccessor(v8::Isolate *isolate, v8::Local<v8::FunctionTemplate> tpl, const char *name, v8::AccessorGetterCallback getter,
                           v8::AccessorSetterCallback setter = nullptr) {
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
        } else {
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
        } else {
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
        } else {
            bool r1 = SafeToNumber(stack.Pop(), ctx, w);
            bool r2 = SafeToNumber(stack.Pop(), ctx, x);
            bool r3 = SafeToNumber(stack.Pop(), ctx, y);
            bool r4 = SafeToNumber(stack.Pop(), ctx, z);
            return r1 && r2 && r3 && r4;
        }
        return false;
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
