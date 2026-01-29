#include "vector4.h"
#include "../v8_helpers.h"

#include <glm/gtx/norm.hpp>
#include <sstream>

namespace Framework::Scripting::JS::Builtins {

    v8::Global<v8::FunctionTemplate> Vector4::_template;

    void Vector4::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8::Local<v8::FunctionTemplate> tmpl = GetTemplate(isolate);
        v8::Local<v8::Function> constructor = tmpl->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();

        global
            ->Set(isolate->GetCurrentContext(), v8::String::NewFromUtf8(isolate, "Vector4").ToLocalChecked(),
                  constructor)
            .Check();
    }

    v8::Local<v8::FunctionTemplate> Vector4::GetTemplate(v8::Isolate *isolate) {
        if (!_template.IsEmpty()) {
            return _template.Get(isolate);
        }

        v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, New);
        tmpl->SetClassName(v8::String::NewFromUtf8(isolate, "Vector4").ToLocalChecked());
        tmpl->InstanceTemplate()->SetInternalFieldCount(1);

        v8::Local<v8::ObjectTemplate> instanceTmpl = tmpl->InstanceTemplate();
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "x").ToLocalChecked(), GetX, SetX);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "y").ToLocalChecked(), GetY, SetY);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "z").ToLocalChecked(), GetZ, SetZ);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "w").ToLocalChecked(), GetW, SetW);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "length").ToLocalChecked(), GetLength);

        v8::Local<v8::ObjectTemplate> protoTmpl = tmpl->PrototypeTemplate();
        protoTmpl->Set(isolate, "add", v8::FunctionTemplate::New(isolate, Add));
        protoTmpl->Set(isolate, "sub", v8::FunctionTemplate::New(isolate, Sub));
        protoTmpl->Set(isolate, "mul", v8::FunctionTemplate::New(isolate, Mul));
        protoTmpl->Set(isolate, "div", v8::FunctionTemplate::New(isolate, Div));
        protoTmpl->Set(isolate, "dot", v8::FunctionTemplate::New(isolate, Dot));
        protoTmpl->Set(isolate, "normalize", v8::FunctionTemplate::New(isolate, Normalize));
        protoTmpl->Set(isolate, "lerp", v8::FunctionTemplate::New(isolate, Lerp));
        protoTmpl->Set(isolate, "clone", v8::FunctionTemplate::New(isolate, Clone));
        protoTmpl->Set(isolate, "toArray", v8::FunctionTemplate::New(isolate, ToArray));
        protoTmpl->Set(isolate, "toString", v8::FunctionTemplate::New(isolate, ToString));

        tmpl->Set(isolate, "zero", v8::FunctionTemplate::New(isolate, Zero));
        tmpl->Set(isolate, "one", v8::FunctionTemplate::New(isolate, One));

        _template.Reset(isolate, tmpl);
        return tmpl;
    }

    v8::Local<v8::Object> Vector4::NewInstance(v8::Isolate *isolate, float x, float y, float z, float w) {
        v8::Local<v8::FunctionTemplate> tmpl = GetTemplate(isolate);
        v8::Local<v8::Function> constructor = tmpl->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();

        v8::Local<v8::Value> argv[] = {v8::Number::New(isolate, x), v8::Number::New(isolate, y),
                                       v8::Number::New(isolate, z), v8::Number::New(isolate, w)};

        return constructor->NewInstance(isolate->GetCurrentContext(), 4, argv).ToLocalChecked();
    }

    v8::Local<v8::Object> Vector4::NewInstance(v8::Isolate *isolate, const glm::vec4 &vec) {
        return NewInstance(isolate, vec.x, vec.y, vec.z, vec.w);
    }

    glm::vec4 *Vector4::Unwrap(v8::Local<v8::Object> obj) {
        v8::Local<v8::Value> field = obj->GetInternalField(0);
        if (field.IsEmpty() || !field->IsExternal())
            return nullptr;
        return static_cast<glm::vec4 *>(field.As<v8::External>()->Value());
    }

    bool Vector4::IsInstance(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (_template.IsEmpty() || !value->IsObject())
            return false;
        return _template.Get(isolate)->HasInstance(value);
    }

    void Vector4::New(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        if (!args.IsConstructCall()) {
            V8Helpers::ThrowTypeError(isolate, "Vector4 must be called with new");
            return;
        }

        float x = V8Helpers::GetFloat(args, 0, 0.0f);
        float y = V8Helpers::GetFloat(args, 1, 0.0f);
        float z = V8Helpers::GetFloat(args, 2, 0.0f);
        float w = V8Helpers::GetFloat(args, 3, 0.0f);

        glm::vec4 *vec = new glm::vec4(x, y, z, w);
        args.This()->SetInternalField(0, v8::External::New(isolate, vec));
        args.GetReturnValue().Set(args.This());
    }

    void Vector4::GetX(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec4 *vec = Unwrap(info.Holder());
        if (vec)
            info.GetReturnValue().Set(vec->x);
    }

    void Vector4::SetX(v8::Local<v8::String>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
        glm::vec4 *vec = Unwrap(info.Holder());
        if (vec && value->IsNumber())
            vec->x = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Vector4::GetY(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec4 *vec = Unwrap(info.Holder());
        if (vec)
            info.GetReturnValue().Set(vec->y);
    }

    void Vector4::SetY(v8::Local<v8::String>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
        glm::vec4 *vec = Unwrap(info.Holder());
        if (vec && value->IsNumber())
            vec->y = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Vector4::GetZ(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec4 *vec = Unwrap(info.Holder());
        if (vec)
            info.GetReturnValue().Set(vec->z);
    }

    void Vector4::SetZ(v8::Local<v8::String>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
        glm::vec4 *vec = Unwrap(info.Holder());
        if (vec && value->IsNumber())
            vec->z = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Vector4::GetW(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec4 *vec = Unwrap(info.Holder());
        if (vec)
            info.GetReturnValue().Set(vec->w);
    }

    void Vector4::SetW(v8::Local<v8::String>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void> &info) {
        glm::vec4 *vec = Unwrap(info.Holder());
        if (vec && value->IsNumber())
            vec->w = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Vector4::GetLength(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec4 *vec = Unwrap(info.Holder());
        if (vec)
            info.GetReturnValue().Set(glm::length(*vec));
    }

    void Vector4::Add(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec4 other;
        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec4 *otherVec = Unwrap(args[0].As<v8::Object>());
            if (otherVec)
                other = *otherVec;
        } else {
            other.x = V8Helpers::GetFloat(args, 0, 0.0f);
            other.y = V8Helpers::GetFloat(args, 1, 0.0f);
            other.z = V8Helpers::GetFloat(args, 2, 0.0f);
            other.w = V8Helpers::GetFloat(args, 3, 0.0f);
        }

        args.GetReturnValue().Set(NewInstance(isolate, *vec + other));
    }

    void Vector4::Sub(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec4 other;
        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec4 *otherVec = Unwrap(args[0].As<v8::Object>());
            if (otherVec)
                other = *otherVec;
        } else {
            other.x = V8Helpers::GetFloat(args, 0, 0.0f);
            other.y = V8Helpers::GetFloat(args, 1, 0.0f);
            other.z = V8Helpers::GetFloat(args, 2, 0.0f);
            other.w = V8Helpers::GetFloat(args, 3, 0.0f);
        }

        args.GetReturnValue().Set(NewInstance(isolate, *vec - other));
    }

    void Vector4::Mul(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        if (args.Length() == 1 && args[0]->IsNumber()) {
            float scalar = static_cast<float>(args[0].As<v8::Number>()->Value());
            args.GetReturnValue().Set(NewInstance(isolate, *vec * scalar));
        } else {
            glm::vec4 other(1.0f);
            if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
                glm::vec4 *otherVec = Unwrap(args[0].As<v8::Object>());
                if (otherVec)
                    other = *otherVec;
            } else {
                other.x = V8Helpers::GetFloat(args, 0, 1.0f);
                other.y = V8Helpers::GetFloat(args, 1, 1.0f);
                other.z = V8Helpers::GetFloat(args, 2, 1.0f);
                other.w = V8Helpers::GetFloat(args, 3, 1.0f);
            }
            args.GetReturnValue().Set(NewInstance(isolate, *vec * other));
        }
    }

    void Vector4::Div(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        if (args.Length() == 1 && args[0]->IsNumber()) {
            float scalar = static_cast<float>(args[0].As<v8::Number>()->Value());
            if (scalar == 0.0f) {
                V8Helpers::ThrowError(isolate, "Division by zero");
                return;
            }
            args.GetReturnValue().Set(NewInstance(isolate, *vec / scalar));
        } else {
            glm::vec4 other(1.0f);
            if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
                glm::vec4 *otherVec = Unwrap(args[0].As<v8::Object>());
                if (otherVec)
                    other = *otherVec;
            } else {
                other.x = V8Helpers::GetFloat(args, 0, 1.0f);
                other.y = V8Helpers::GetFloat(args, 1, 1.0f);
                other.z = V8Helpers::GetFloat(args, 2, 1.0f);
                other.w = V8Helpers::GetFloat(args, 3, 1.0f);
            }

            if (other.x == 0.0f || other.y == 0.0f || other.z == 0.0f || other.w == 0.0f) {
                V8Helpers::ThrowError(isolate, "Division by zero");
                return;
            }
            args.GetReturnValue().Set(NewInstance(isolate, *vec / other));
        }
    }

    void Vector4::Dot(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec4 other;
        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec4 *otherVec = Unwrap(args[0].As<v8::Object>());
            if (otherVec)
                other = *otherVec;
        } else {
            other.x = V8Helpers::GetFloat(args, 0, 0.0f);
            other.y = V8Helpers::GetFloat(args, 1, 0.0f);
            other.z = V8Helpers::GetFloat(args, 2, 0.0f);
            other.w = V8Helpers::GetFloat(args, 3, 0.0f);
        }

        args.GetReturnValue().Set(glm::dot(*vec, other));
    }

    void Vector4::Normalize(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        float len = glm::length(*vec);
        if (len > 0.0f) {
            args.GetReturnValue().Set(NewInstance(isolate, *vec / len));
        } else {
            args.GetReturnValue().Set(NewInstance(isolate, glm::vec4(0.0f)));
        }
    }

    void Vector4::Lerp(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec4 target;
        float t = 0.5f;

        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec4 *targetVec = Unwrap(args[0].As<v8::Object>());
            if (targetVec)
                target = *targetVec;
            t = V8Helpers::GetFloat(args, 1, 0.5f);
        } else {
            target.x = V8Helpers::GetFloat(args, 0, 0.0f);
            target.y = V8Helpers::GetFloat(args, 1, 0.0f);
            target.z = V8Helpers::GetFloat(args, 2, 0.0f);
            target.w = V8Helpers::GetFloat(args, 3, 0.0f);
            t = V8Helpers::GetFloat(args, 4, 0.5f);
        }

        args.GetReturnValue().Set(NewInstance(isolate, glm::mix(*vec, target, t)));
    }

    void Vector4::Clone(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (vec)
            args.GetReturnValue().Set(NewInstance(isolate, *vec));
    }

    void Vector4::ToArray(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        v8::Local<v8::Array> arr = v8::Array::New(isolate, 4);
        arr->Set(isolate->GetCurrentContext(), 0, v8::Number::New(isolate, vec->x)).Check();
        arr->Set(isolate->GetCurrentContext(), 1, v8::Number::New(isolate, vec->y)).Check();
        arr->Set(isolate->GetCurrentContext(), 2, v8::Number::New(isolate, vec->z)).Check();
        arr->Set(isolate->GetCurrentContext(), 3, v8::Number::New(isolate, vec->w)).Check();

        args.GetReturnValue().Set(arr);
    }

    void Vector4::ToString(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec4 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        std::ostringstream ss;
        ss << "Vector4(" << vec->x << ", " << vec->y << ", " << vec->z << ", " << vec->w << ")";

        args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, ss.str().c_str()).ToLocalChecked());
    }

    void Vector4::Zero(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 0.0f, 0.0f, 0.0f, 0.0f));
    }

    void Vector4::One(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 1.0f, 1.0f, 1.0f, 1.0f));
    }

} // namespace Framework::Scripting::JS::Builtins
