#include "quaternion.h"
#include "../v8_helpers.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <sstream>

namespace Framework::Scripting::JS::Builtins {

    v8::Global<v8::FunctionTemplate> Quaternion::_template;

    void Quaternion::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8::Local<v8::FunctionTemplate> tmpl = GetTemplate(isolate);
        v8::Local<v8::Function> constructor = tmpl->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();

        global
            ->Set(isolate->GetCurrentContext(), v8::String::NewFromUtf8(isolate, "Quaternion").ToLocalChecked(),
                  constructor)
            .Check();
    }

    v8::Local<v8::FunctionTemplate> Quaternion::GetTemplate(v8::Isolate *isolate) {
        if (!_template.IsEmpty()) {
            return _template.Get(isolate);
        }

        v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, New);
        tmpl->SetClassName(v8::String::NewFromUtf8(isolate, "Quaternion").ToLocalChecked());
        tmpl->InstanceTemplate()->SetInternalFieldCount(1);

        v8::Local<v8::ObjectTemplate> instanceTmpl = tmpl->InstanceTemplate();
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "w").ToLocalChecked(), GetW, SetW);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "x").ToLocalChecked(), GetX, SetX);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "y").ToLocalChecked(), GetY, SetY);
        instanceTmpl->SetAccessor(v8::String::NewFromUtf8(isolate, "z").ToLocalChecked(), GetZ, SetZ);

        v8::Local<v8::ObjectTemplate> protoTmpl = tmpl->PrototypeTemplate();
        protoTmpl->Set(isolate, "multiply", v8::FunctionTemplate::New(isolate, Multiply));
        protoTmpl->Set(isolate, "normalize", v8::FunctionTemplate::New(isolate, Normalize));
        protoTmpl->Set(isolate, "conjugate", v8::FunctionTemplate::New(isolate, Conjugate));
        protoTmpl->Set(isolate, "inverse", v8::FunctionTemplate::New(isolate, Inverse));
        protoTmpl->Set(isolate, "slerp", v8::FunctionTemplate::New(isolate, Slerp));
        protoTmpl->Set(isolate, "dot", v8::FunctionTemplate::New(isolate, Dot));
        protoTmpl->Set(isolate, "rotateVector", v8::FunctionTemplate::New(isolate, RotateVector));
        protoTmpl->Set(isolate, "toEuler", v8::FunctionTemplate::New(isolate, ToEuler));
        protoTmpl->Set(isolate, "clone", v8::FunctionTemplate::New(isolate, Clone));
        protoTmpl->Set(isolate, "toArray", v8::FunctionTemplate::New(isolate, ToArray));
        protoTmpl->Set(isolate, "toString", v8::FunctionTemplate::New(isolate, ToString));

        tmpl->Set(isolate, "identity", v8::FunctionTemplate::New(isolate, Identity));
        tmpl->Set(isolate, "fromEuler", v8::FunctionTemplate::New(isolate, FromEuler));
        tmpl->Set(isolate, "fromAxisAngle", v8::FunctionTemplate::New(isolate, FromAxisAngle));
        tmpl->Set(isolate, "lookRotation", v8::FunctionTemplate::New(isolate, LookRotation));

        _template.Reset(isolate, tmpl);
        return tmpl;
    }

    v8::Local<v8::Object> Quaternion::NewInstance(v8::Isolate *isolate, float w, float x, float y, float z) {
        v8::Local<v8::FunctionTemplate> tmpl = GetTemplate(isolate);
        v8::Local<v8::Function> constructor = tmpl->GetFunction(isolate->GetCurrentContext()).ToLocalChecked();

        v8::Local<v8::Value> argv[] = {v8::Number::New(isolate, w), v8::Number::New(isolate, x),
                                       v8::Number::New(isolate, y), v8::Number::New(isolate, z)};

        return constructor->NewInstance(isolate->GetCurrentContext(), 4, argv).ToLocalChecked();
    }

    v8::Local<v8::Object> Quaternion::NewInstance(v8::Isolate *isolate, const glm::quat &quat) {
        return NewInstance(isolate, quat.w, quat.x, quat.y, quat.z);
    }

    glm::quat *Quaternion::Unwrap(v8::Local<v8::Object> obj) {
        v8::Local<v8::Value> field = obj->GetInternalField(0);
        if (field.IsEmpty() || !field->IsExternal())
            return nullptr;
        return static_cast<glm::quat *>(field.As<v8::External>()->Value());
    }

    bool Quaternion::IsInstance(v8::Isolate *isolate, v8::Local<v8::Value> value) {
        if (_template.IsEmpty() || !value->IsObject())
            return false;
        return _template.Get(isolate)->HasInstance(value);
    }

    void Quaternion::New(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        if (!args.IsConstructCall()) {
            V8Helpers::ThrowTypeError(isolate, "Quaternion must be called with new");
            return;
        }

        float w = V8Helpers::GetFloat(args, 0, 1.0f);
        float x = V8Helpers::GetFloat(args, 1, 0.0f);
        float y = V8Helpers::GetFloat(args, 2, 0.0f);
        float z = V8Helpers::GetFloat(args, 3, 0.0f);

        glm::quat *quat = new glm::quat(w, x, y, z);
        args.This()->SetInternalField(0, v8::External::New(isolate, quat));
        args.GetReturnValue().Set(args.This());
    }

    void Quaternion::GetW(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::quat *quat = Unwrap(info.Holder());
        if (quat)
            info.GetReturnValue().Set(quat->w);
    }

    void Quaternion::SetW(v8::Local<v8::String>, v8::Local<v8::Value> value,
                          const v8::PropertyCallbackInfo<void> &info) {
        glm::quat *quat = Unwrap(info.Holder());
        if (quat && value->IsNumber())
            quat->w = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Quaternion::GetX(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::quat *quat = Unwrap(info.Holder());
        if (quat)
            info.GetReturnValue().Set(quat->x);
    }

    void Quaternion::SetX(v8::Local<v8::String>, v8::Local<v8::Value> value,
                          const v8::PropertyCallbackInfo<void> &info) {
        glm::quat *quat = Unwrap(info.Holder());
        if (quat && value->IsNumber())
            quat->x = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Quaternion::GetY(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::quat *quat = Unwrap(info.Holder());
        if (quat)
            info.GetReturnValue().Set(quat->y);
    }

    void Quaternion::SetY(v8::Local<v8::String>, v8::Local<v8::Value> value,
                          const v8::PropertyCallbackInfo<void> &info) {
        glm::quat *quat = Unwrap(info.Holder());
        if (quat && value->IsNumber())
            quat->y = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Quaternion::GetZ(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value> &info) {
        glm::quat *quat = Unwrap(info.Holder());
        if (quat)
            info.GetReturnValue().Set(quat->z);
    }

    void Quaternion::SetZ(v8::Local<v8::String>, v8::Local<v8::Value> value,
                          const v8::PropertyCallbackInfo<void> &info) {
        glm::quat *quat = Unwrap(info.Holder());
        if (quat && value->IsNumber())
            quat->z = static_cast<float>(value.As<v8::Number>()->Value());
    }

    void Quaternion::Multiply(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (!quat)
            return;

        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::quat *other = Unwrap(args[0].As<v8::Object>());
            if (other) {
                args.GetReturnValue().Set(NewInstance(isolate, *quat * *other));
            }
        } else {
            V8Helpers::ThrowTypeError(isolate, "Expected Quaternion argument");
        }
    }

    void Quaternion::Normalize(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (quat) {
            args.GetReturnValue().Set(NewInstance(isolate, glm::normalize(*quat)));
        }
    }

    void Quaternion::Conjugate(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (quat) {
            args.GetReturnValue().Set(NewInstance(isolate, glm::conjugate(*quat)));
        }
    }

    void Quaternion::Inverse(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (quat) {
            args.GetReturnValue().Set(NewInstance(isolate, glm::inverse(*quat)));
        }
    }

    void Quaternion::Slerp(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (!quat)
            return;

        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::quat *target = Unwrap(args[0].As<v8::Object>());
            float t = V8Helpers::GetFloat(args, 1, 0.5f);
            if (target) {
                args.GetReturnValue().Set(NewInstance(isolate, glm::slerp(*quat, *target, t)));
            }
        } else {
            V8Helpers::ThrowTypeError(isolate, "Expected Quaternion argument");
        }
    }

    void Quaternion::Dot(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (!quat)
            return;

        if (args.Length() >= 1 && IsInstance(isolate, args[0])) {
            glm::quat *other = Unwrap(args[0].As<v8::Object>());
            if (other) {
                args.GetReturnValue().Set(glm::dot(*quat, *other));
            }
        } else {
            V8Helpers::ThrowTypeError(isolate, "Expected Quaternion argument");
        }
    }

    void Quaternion::RotateVector(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (!quat)
            return;

        glm::vec3 vec;
        vec.x = V8Helpers::GetFloat(args, 0, 0.0f);
        vec.y = V8Helpers::GetFloat(args, 1, 0.0f);
        vec.z = V8Helpers::GetFloat(args, 2, 0.0f);

        glm::vec3 result = *quat * vec;

        // Return as array [x, y, z] since Vector3 might not be registered yet
        v8::Local<v8::Array> arr = v8::Array::New(isolate, 3);
        arr->Set(isolate->GetCurrentContext(), 0, v8::Number::New(isolate, result.x)).Check();
        arr->Set(isolate->GetCurrentContext(), 1, v8::Number::New(isolate, result.y)).Check();
        arr->Set(isolate->GetCurrentContext(), 2, v8::Number::New(isolate, result.z)).Check();
        args.GetReturnValue().Set(arr);
    }

    void Quaternion::ToEuler(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (!quat)
            return;

        glm::vec3 euler = glm::eulerAngles(*quat);

        // Return as array [pitch, yaw, roll] in radians
        v8::Local<v8::Array> arr = v8::Array::New(isolate, 3);
        arr->Set(isolate->GetCurrentContext(), 0, v8::Number::New(isolate, euler.x)).Check();
        arr->Set(isolate->GetCurrentContext(), 1, v8::Number::New(isolate, euler.y)).Check();
        arr->Set(isolate->GetCurrentContext(), 2, v8::Number::New(isolate, euler.z)).Check();
        args.GetReturnValue().Set(arr);
    }

    void Quaternion::Clone(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (quat)
            args.GetReturnValue().Set(NewInstance(isolate, *quat));
    }

    void Quaternion::ToArray(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (!quat)
            return;

        v8::Local<v8::Array> arr = v8::Array::New(isolate, 4);
        arr->Set(isolate->GetCurrentContext(), 0, v8::Number::New(isolate, quat->w)).Check();
        arr->Set(isolate->GetCurrentContext(), 1, v8::Number::New(isolate, quat->x)).Check();
        arr->Set(isolate->GetCurrentContext(), 2, v8::Number::New(isolate, quat->y)).Check();
        arr->Set(isolate->GetCurrentContext(), 3, v8::Number::New(isolate, quat->z)).Check();
        args.GetReturnValue().Set(arr);
    }

    void Quaternion::ToString(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        glm::quat *quat = Unwrap(args.Holder());
        if (!quat)
            return;

        std::ostringstream ss;
        ss << "Quaternion(" << quat->w << ", " << quat->x << ", " << quat->y << ", " << quat->z << ")";
        args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, ss.str().c_str()).ToLocalChecked());
    }

    void Quaternion::Identity(const v8::FunctionCallbackInfo<v8::Value> &args) {
        args.GetReturnValue().Set(NewInstance(args.GetIsolate(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
    }

    void Quaternion::FromEuler(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        float pitch = V8Helpers::GetFloat(args, 0, 0.0f);
        float yaw = V8Helpers::GetFloat(args, 1, 0.0f);
        float roll = V8Helpers::GetFloat(args, 2, 0.0f);

        glm::quat quat = glm::quat(glm::vec3(pitch, yaw, roll));
        args.GetReturnValue().Set(NewInstance(isolate, quat));
    }

    void Quaternion::FromAxisAngle(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        float axisX = V8Helpers::GetFloat(args, 0, 0.0f);
        float axisY = V8Helpers::GetFloat(args, 1, 1.0f);
        float axisZ = V8Helpers::GetFloat(args, 2, 0.0f);
        float angle = V8Helpers::GetFloat(args, 3, 0.0f);

        glm::vec3 axis = glm::normalize(glm::vec3(axisX, axisY, axisZ));
        glm::quat quat = glm::angleAxis(angle, axis);
        args.GetReturnValue().Set(NewInstance(isolate, quat));
    }

    void Quaternion::LookRotation(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        float forwardX = V8Helpers::GetFloat(args, 0, 0.0f);
        float forwardY = V8Helpers::GetFloat(args, 1, 0.0f);
        float forwardZ = V8Helpers::GetFloat(args, 2, 1.0f);
        float upX = V8Helpers::GetFloat(args, 3, 0.0f);
        float upY = V8Helpers::GetFloat(args, 4, 1.0f);
        float upZ = V8Helpers::GetFloat(args, 5, 0.0f);

        glm::vec3 forward = glm::normalize(glm::vec3(forwardX, forwardY, forwardZ));
        glm::vec3 up = glm::normalize(glm::vec3(upX, upY, upZ));

        glm::quat quat = glm::quatLookAt(forward, up);
        args.GetReturnValue().Set(NewInstance(isolate, quat));
    }

} // namespace Framework::Scripting::JS::Builtins
