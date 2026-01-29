#pragma once

#include <v8.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Framework::Scripting::JS::Builtins {

    /**
     * V8 binding for Quaternion type.
     * Uses glm::quat internally (w, x, y, z order).
     */
    class Quaternion {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8::Local<v8::FunctionTemplate> GetTemplate(v8::Isolate *isolate);
        static v8::Local<v8::Object> NewInstance(v8::Isolate *isolate, float w, float x, float y, float z);
        static v8::Local<v8::Object> NewInstance(v8::Isolate *isolate, const glm::quat &quat);
        static glm::quat *Unwrap(v8::Local<v8::Object> obj);
        static bool IsInstance(v8::Isolate *isolate, v8::Local<v8::Value> value);

      private:
        static void New(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Properties
        static void GetW(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetW(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);
        static void GetX(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetX(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);
        static void GetY(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetY(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);
        static void GetZ(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetZ(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);

        // Methods
        static void Multiply(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Normalize(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Conjugate(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Inverse(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Slerp(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Dot(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void RotateVector(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ToEuler(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Clone(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ToArray(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ToString(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Static methods
        static void Identity(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void FromEuler(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void FromAxisAngle(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void LookRotation(const v8::FunctionCallbackInfo<v8::Value> &args);

        static v8::Global<v8::FunctionTemplate> _template;
    };

} // namespace Framework::Scripting::JS::Builtins
