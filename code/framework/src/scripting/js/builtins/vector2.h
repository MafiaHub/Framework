#pragma once

#include <v8.h>
#include <glm/glm.hpp>

namespace Framework::Scripting::JS::Builtins {

    /**
     * V8 binding for Vector2 type.
     */
    class Vector2 {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8::Local<v8::FunctionTemplate> GetTemplate(v8::Isolate *isolate);
        static v8::Local<v8::Object> NewInstance(v8::Isolate *isolate, float x, float y);
        static v8::Local<v8::Object> NewInstance(v8::Isolate *isolate, const glm::vec2 &vec);
        static glm::vec2 *Unwrap(v8::Local<v8::Object> obj);
        static bool IsInstance(v8::Isolate *isolate, v8::Local<v8::Value> value);

      private:
        static void New(const v8::FunctionCallbackInfo<v8::Value> &args);

        static void GetX(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetX(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);
        static void GetY(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetY(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);
        static void GetLength(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void GetLengthSquared(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);

        static void Add(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Sub(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Mul(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Div(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Dot(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Normalize(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Lerp(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Distance(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Clone(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ToArray(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ToString(const v8::FunctionCallbackInfo<v8::Value> &args);

        static void Zero(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void One(const v8::FunctionCallbackInfo<v8::Value> &args);

        static v8::Global<v8::FunctionTemplate> _template;
    };

} // namespace Framework::Scripting::JS::Builtins
