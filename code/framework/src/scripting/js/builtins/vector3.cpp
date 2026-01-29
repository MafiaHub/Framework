#include "vector3.h"
#include "../v8_helpers.h"

#include <glm/gtx/norm.hpp>
#include <sstream>

namespace Framework::Scripting::JS::Builtins {

    v8::Global<v8::FunctionTemplate> Vector3::_template;

    void Vector3::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8::Local<v8::FunctionTemplate> tmpl = GetTemplate(isolate);
        v8::Local<v8::Function> constructor = tmpl->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();

        global
            ->Set(isolate->GetCurrentContext(), v8::String::NewFromUtf8(isolate, "Vector3").ToLocalChecked(),
                  constructor)
            .Check();
    }

    v8::Local<v8::FunctionTemplate> Vector3::GetTemplate(v8::Isolate *isolate) {
        if (!_template.IsEmpty()) {
            return _template.Get(isolate);
        }

        v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, New);
        tmpl->SetClassName(v8::String::NewFromUtf8(isolate, "Vector3").ToLocalChecked());
        tmpl->InstanceTemplate()->SetInternalFieldCount(1);

        // Properties with getters and setters
        v8::Local<v8::ObjectTemplate> instanceTmpl = tmpl->InstanceTemplate();
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "x").ToLocalChecked(), GetX, SetX);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "y").ToLocalChecked(), GetY, SetY);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "z").ToLocalChecked(), GetZ, SetZ);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "length").ToLocalChecked(), GetLength);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "lengthSquared").ToLocalChecked(), GetLengthSquared);

        // Prototype methods
        v8::Local<v8::ObjectTemplate> protoTmpl = tmpl->PrototypeTemplate();
        protoTmpl->Set(isolate, "add", v8::FunctionTemplate::New(isolate, Add));
        protoTmpl->Set(isolate, "sub", v8::FunctionTemplate::New(isolate, Sub));
        protoTmpl->Set(isolate, "mul", v8::FunctionTemplate::New(isolate, Mul));
        protoTmpl->Set(isolate, "div", v8::FunctionTemplate::New(isolate, Div));
        protoTmpl->Set(isolate, "dot", v8::FunctionTemplate::New(isolate, Dot));
        protoTmpl->Set(isolate, "cross", v8::FunctionTemplate::New(isolate, Cross));
        protoTmpl->Set(isolate, "normalize", v8::FunctionTemplate::New(isolate, Normalize));
        protoTmpl->Set(isolate, "lerp", v8::FunctionTemplate::New(isolate, Lerp));
        protoTmpl->Set(isolate, "distance", v8::FunctionTemplate::New(isolate, Distance));
        protoTmpl->Set(isolate, "clone", v8::FunctionTemplate::New(isolate, Clone));
        protoTmpl->Set(isolate, "toArray", v8::FunctionTemplate::New(isolate, ToArray));
        protoTmpl->Set(isolate, "toString", v8::FunctionTemplate::New(isolate, ToString));

        // Static methods
        tmpl->Set(isolate, "zero", v8::FunctionTemplate::New(isolate, Zero));
        tmpl->Set(isolate, "one", v8::FunctionTemplate::New(isolate, One));
        tmpl->Set(isolate, "up", v8::FunctionTemplate::New(isolate, Up));
        tmpl->Set(isolate, "forward", v8::FunctionTemplate::New(isolate, Forward));
        tmpl->Set(isolate, "right", v8::FunctionTemplate::New(isolate, Right));

        _template.Reset(isolate, tmpl);
        return tmpl;
    }

    v8::Local<v8::Object> Vector3::NewInstance(v8::Isolate *isolate, float x, float y, float z) {
        v8::Local<v8::FunctionTemplate> tmpl = GetTemplate(isolate);
        v8::Local<v8::Function> constructor = tmpl->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();

        v8::Local<v8::Value> argv[] = {v8::Number::New(isolate, x), v8::Number::New(isolate, y),
                                       v8::Number::New(isolate, z)};

        return constructor->NewInstance(isolate->GetCurrentContext(), 3, argv).ToLocalChecked();
    }

    v8::Local<v8::Object> Vector3::NewInstance(v8::Isolate *isolate, const glm::vec3 &vec) {
        return NewInstance(isolate, vec.x, vec.y, vec.z);
    }

    glm::vec3 *Vector3::Unwrap(v8::Local<v8::Object> obj) {
        v8::Local<v8::Value> field = obj->GetInternalField(0);
        if (field.IsEmpty() || !field->IsExternal()) {
            return nullptr;
        }
        return static_cast<glm::vec3 *>(field.As<v8::External>()->Value());
    }

    bool Vector3::IsInstance(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (_template.IsEmpty() || !value->IsObject()) {
            return false;
        }
        return _template.Get(isolate)->HasInstance(value);
    }

    void Vector3::New(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        if (!args.IsConstructCall()) {
            V8Helpers::ThrowTypeError(isolate, "Vector3 must be called with new");
            return;
        }

        float x = V8Helpers::GetFloat(args, 0, 0.0f);
        float y = V8Helpers::GetFloat(args, 1, 0.0f);
        float z = V8Helpers::GetFloat(args, 2, 0.0f);

        glm::vec3 *vec = new glm::vec3(x, y, z);
        args.This()->SetInternalField(0, v8::External::New(isolate, vec));
        args.GetReturnValue().Set(args.This());
    }

    void Vector3::GetX(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec3 *vec = Unwrap(info.Holder());
        if (vec) {
            info.GetReturnValue().Set(vec->x);
        }
    }

    void Vector3::SetX(v8::Local<v8::String>, v8::Local<v8::Value> value,
                       const v8::PropertyCallbackInfo<void> &info) {
        glm::vec3 *vec = Unwrap(info.Holder());
        if (vec && value->IsNumber()) {
            vec->x = static_cast<float>(value.As<v8::Number>()->Value());
        }
    }

    void Vector3::GetY(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec3 *vec = Unwrap(info.Holder());
        if (vec) {
            info.GetReturnValue().Set(vec->y);
        }
    }

    void Vector3::SetY(v8::Local<v8::String>, v8::Local<v8::Value> value,
                       const v8::PropertyCallbackInfo<void> &info) {
        glm::vec3 *vec = Unwrap(info.Holder());
        if (vec && value->IsNumber()) {
            vec->y = static_cast<float>(value.As<v8::Number>()->Value());
        }
    }

    void Vector3::GetZ(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec3 *vec = Unwrap(info.Holder());
        if (vec) {
            info.GetReturnValue().Set(vec->z);
        }
    }

    void Vector3::SetZ(v8::Local<v8::String>, v8::Local<v8::Value> value,
                       const v8::PropertyCallbackInfo<void> &info) {
        glm::vec3 *vec = Unwrap(info.Holder());
        if (vec && value->IsNumber()) {
            vec->z = static_cast<float>(value.As<v8::Number>()->Value());
        }
    }

    void Vector3::GetLength(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec3 *vec = Unwrap(info.Holder());
        if (vec) {
            info.GetReturnValue().Set(glm::length(*vec));
        }
    }

    void Vector3::GetLengthSquared(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::vec3 *vec = Unwrap(info.Holder());
        if (vec) {
            info.GetReturnValue().Set(glm::length2(*vec));
        }
    }

    void Vector3::Add(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec3 other;
        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec3 *otherVec = Unwrap(args[0].As<v8::Object>());
            if (otherVec)
                other = *otherVec;
        } else {
            other.x = V8Helpers::GetFloat(args, 0, 0.0f);
            other.y = V8Helpers::GetFloat(args, 1, 0.0f);
            other.z = V8Helpers::GetFloat(args, 2, 0.0f);
        }

        args.GetReturnValue().Set(NewInstance(isolate, *vec + other));
    }

    void Vector3::Sub(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec3 other;
        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec3 *otherVec = Unwrap(args[0].As<v8::Object>());
            if (otherVec)
                other = *otherVec;
        } else {
            other.x = V8Helpers::GetFloat(args, 0, 0.0f);
            other.y = V8Helpers::GetFloat(args, 1, 0.0f);
            other.z = V8Helpers::GetFloat(args, 2, 0.0f);
        }

        args.GetReturnValue().Set(NewInstance(isolate, *vec - other));
    }

    void Vector3::Mul(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        if (args.Length() == 1 && args[0]->IsNumber()) {
            // Scalar multiplication
            float scalar = static_cast<float>(args[0].As<v8::Number>()->Value());
            args.GetReturnValue().Set(NewInstance(isolate, *vec * scalar));
        } else {
            // Component-wise multiplication
            glm::vec3 other;
            if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
                glm::vec3 *otherVec = Unwrap(args[0].As<v8::Object>());
                if (otherVec)
                    other = *otherVec;
            } else {
                other.x = V8Helpers::GetFloat(args, 0, 1.0f);
                other.y = V8Helpers::GetFloat(args, 1, 1.0f);
                other.z = V8Helpers::GetFloat(args, 2, 1.0f);
            }
            args.GetReturnValue().Set(NewInstance(isolate, *vec * other));
        }
    }

    void Vector3::Div(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        if (args.Length() == 1 && args[0]->IsNumber()) {
            // Scalar division
            float scalar = static_cast<float>(args[0].As<v8::Number>()->Value());
            if (scalar == 0.0f) {
                V8Helpers::ThrowError(isolate, "Division by zero");
                return;
            }
            args.GetReturnValue().Set(NewInstance(isolate, *vec / scalar));
        } else {
            // Component-wise division
            glm::vec3 other;
            if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
                glm::vec3 *otherVec = Unwrap(args[0].As<v8::Object>());
                if (otherVec)
                    other = *otherVec;
            } else {
                other.x = V8Helpers::GetFloat(args, 0, 1.0f);
                other.y = V8Helpers::GetFloat(args, 1, 1.0f);
                other.z = V8Helpers::GetFloat(args, 2, 1.0f);
            }

            if (other.x == 0.0f || other.y == 0.0f || other.z == 0.0f) {
                V8Helpers::ThrowError(isolate, "Division by zero");
                return;
            }
            args.GetReturnValue().Set(NewInstance(isolate, *vec / other));
        }
    }

    void Vector3::Dot(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec3 other;
        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec3 *otherVec = Unwrap(args[0].As<v8::Object>());
            if (otherVec)
                other = *otherVec;
        } else {
            other.x = V8Helpers::GetFloat(args, 0, 0.0f);
            other.y = V8Helpers::GetFloat(args, 1, 0.0f);
            other.z = V8Helpers::GetFloat(args, 2, 0.0f);
        }

        args.GetReturnValue().Set(glm::dot(*vec, other));
    }

    void Vector3::Cross(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec3 other;
        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec3 *otherVec = Unwrap(args[0].As<v8::Object>());
            if (otherVec)
                other = *otherVec;
        } else {
            other.x = V8Helpers::GetFloat(args, 0, 0.0f);
            other.y = V8Helpers::GetFloat(args, 1, 0.0f);
            other.z = V8Helpers::GetFloat(args, 2, 0.0f);
        }

        args.GetReturnValue().Set(NewInstance(isolate, glm::cross(*vec, other)));
    }

    void Vector3::Normalize(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        float len = glm::length(*vec);
        if (len > 0.0f) {
            args.GetReturnValue().Set(NewInstance(isolate, *vec / len));
        } else {
            args.GetReturnValue().Set(NewInstance(isolate, glm::vec3(0.0f)));
        }
    }

    void Vector3::Lerp(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec3 target;
        float t = 0.5f;

        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec3 *targetVec = Unwrap(args[0].As<v8::Object>());
            if (targetVec)
                target = *targetVec;
            t = V8Helpers::GetFloat(args, 1, 0.5f);
        } else {
            target.x = V8Helpers::GetFloat(args, 0, 0.0f);
            target.y = V8Helpers::GetFloat(args, 1, 0.0f);
            target.z = V8Helpers::GetFloat(args, 2, 0.0f);
            t = V8Helpers::GetFloat(args, 3, 0.5f);
        }

        args.GetReturnValue().Set(NewInstance(isolate, glm::mix(*vec, target, t)));
    }

    void Vector3::Distance(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        glm::vec3 other;
        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::vec3 *otherVec = Unwrap(args[0].As<v8::Object>());
            if (otherVec)
                other = *otherVec;
        } else {
            other.x = V8Helpers::GetFloat(args, 0, 0.0f);
            other.y = V8Helpers::GetFloat(args, 1, 0.0f);
            other.z = V8Helpers::GetFloat(args, 2, 0.0f);
        }

        args.GetReturnValue().Set(glm::distance(*vec, other));
    }

    void Vector3::Clone(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (vec) {
            args.GetReturnValue().Set(NewInstance(isolate, *vec));
        }
    }

    void Vector3::ToArray(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        v8::Local<v8::Array> arr = v8::Array::New(isolate, 3);
        arr->Set(isolate->GetCurrentContext(), 0, v8::Number::New(isolate, vec->x)).Check();
        arr->Set(isolate->GetCurrentContext(), 1, v8::Number::New(isolate, vec->y)).Check();
        arr->Set(isolate->GetCurrentContext(), 2, v8::Number::New(isolate, vec->z)).Check();

        args.GetReturnValue().Set(arr);
    }

    void Vector3::ToString(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::vec3 *vec = Unwrap(args.Holder());
        if (!vec)
            return;

        std::ostringstream ss;
        ss << "Vector3(" << vec->x << ", " << vec->y << ", " << vec->z << ")";

        args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, ss.str().c_str()).ToLocalChecked());
    }

    void Vector3::Zero(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 0.0f, 0.0f, 0.0f));
    }

    void Vector3::One(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 1.0f, 1.0f, 1.0f));
    }

    void Vector3::Up(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 0.0f, 1.0f, 0.0f));
    }

    void Vector3::Forward(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 0.0f, 0.0f, 1.0f));
    }

    void Vector3::Right(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), 1.0f, 0.0f, 0.0f));
    }

} // namespace Framework::Scripting::JS::Builtins
