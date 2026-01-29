#pragma once

#include <v8.h>
#include <glm/glm.hpp>

namespace Framework::Scripting::JS::Builtins {

    /**
     * V8 binding for Color type (RGBA).
     * Components are stored as floats in range [0, 1].
     */
    class Color {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8::Local<v8::FunctionTemplate> GetTemplate(v8::Isolate *isolate);
        static v8::Local<v8::Object> NewInstance(v8::Isolate *isolate, float r, float g, float b, float a = 1.0f);
        static v8::Local<v8::Object> NewInstance(v8::Isolate *isolate, const glm::vec4 &color);
        static glm::vec4 *Unwrap(v8::Local<v8::Object> obj);
        static bool IsInstance(v8::Isolate *isolate, v8::Local<v8::Value> value);

      private:
        static void New(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Properties
        static void GetR(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetR(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);
        static void GetG(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetG(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);
        static void GetB(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetB(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);
        static void GetA(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value> &info);
        static void SetA(v8::Local<v8::String> property, v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void> &info);

        // Methods
        static void Lerp(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Clone(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ToHex(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ToArray(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void ToString(const v8::FunctionCallbackInfo<v8::Value> &args);

        // Static methods
        static void FromHex(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void FromRGB(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void White(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Black(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Red(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Green(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Blue(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Yellow(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Cyan(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Magenta(const v8::FunctionCallbackInfo<v8::Value> &args);
        static void Transparent(const v8::FunctionCallbackInfo<v8::Value> &args);

        static v8::Global<v8::FunctionTemplate> _template;
    };

} // namespace Framework::Scripting::JS::Builtins
