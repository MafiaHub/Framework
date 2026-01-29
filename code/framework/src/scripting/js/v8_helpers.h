#pragma once

#include <v8.h>
#include <string>

namespace Framework::Scripting::JS {

    /**
     * Helper utilities for V8 bindings.
     */
    class V8Helpers {
      public:
        /**
         * Set a property on an object.
         */
        static void SetProperty(v8::Isolate *isolate, v8::Local<v8::Object> obj, const char *name,
                                v8::Local<v8::Value> value) {
            obj->Set(isolate->GetCurrentContext(), v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), value)
                .Check();
        }

        /**
         * Set a function property.
         */
        static void SetMethod(v8::Isolate *isolate, v8::Local<v8::Object> obj, const char *name,
                              v8::FunctionCallback callback) {
            v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(isolate, callback);
            obj->Set(isolate->GetCurrentContext(), v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
                     tmpl->GetFunction(isolate->GetCurrentContext()).ToLocalChecked())
                .Check();
        }

        /**
         * Set a getter property on an object template.
         */
        static void SetGetter(v8::Isolate *isolate, v8::Local<v8::ObjectTemplate> tmpl, const char *name,
                              v8::AccessorGetterCallback getter) {
            tmpl->SetAccessor(v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), getter);
        }

        /**
         * Set a getter/setter property on an object template.
         */
        static void SetAccessor(v8::Isolate *isolate, v8::Local<v8::ObjectTemplate> tmpl, const char *name,
                                v8::AccessorGetterCallback getter, v8::AccessorSetterCallback setter = nullptr) {
            tmpl->SetAccessor(v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), getter, setter);
        }

        /**
         * Get a double from function arguments.
         */
        static double GetDouble(const v8::FunctionCallbackInfo<v8::Value> &args, int index, double defaultVal = 0.0) {
            if (index < args.Length() && args[index]->IsNumber()) {
                return args[index].As<v8::Number>()->Value();
            }
            return defaultVal;
        }

        /**
         * Get a float from function arguments.
         */
        static float GetFloat(const v8::FunctionCallbackInfo<v8::Value> &args, int index, float defaultVal = 0.0f) {
            return static_cast<float>(GetDouble(args, index, defaultVal));
        }

        /**
         * Get an int from function arguments.
         */
        static int GetInt(const v8::FunctionCallbackInfo<v8::Value> &args, int index, int defaultVal = 0) {
            if (index < args.Length() && args[index]->IsNumber()) {
                return args[index].As<v8::Int32>()->Value();
            }
            return defaultVal;
        }

        /**
         * Get a string from function arguments.
         */
        static std::string GetString(v8::Isolate *isolate, const v8::FunctionCallbackInfo<v8::Value> &args, int index,
                                     const std::string &defaultVal = "") {
            if (index < args.Length() && args[index]->IsString()) {
                v8::String::Utf8Value str(isolate, args[index]);
                return *str ? *str : defaultVal;
            }
            return defaultVal;
        }

        /**
         * Get a bool from function arguments.
         */
        static bool GetBool(const v8::FunctionCallbackInfo<v8::Value> &args, int index, bool defaultVal = false) {
            if (index < args.Length() && args[index]->IsBoolean()) {
                return args[index].As<v8::Boolean>()->Value();
            }
            return defaultVal;
        }

        /**
         * Throw a JavaScript error.
         */
        static void ThrowError(v8::Isolate *isolate, const char *message) {
            isolate->ThrowException(
                v8::Exception::Error(v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
        }

        /**
         * Throw a JavaScript type error.
         */
        static void ThrowTypeError(v8::Isolate *isolate, const char *message) {
            isolate->ThrowException(
                v8::Exception::TypeError(v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
        }

        /**
         * Throw a JavaScript range error.
         */
        static void ThrowRangeError(v8::Isolate *isolate, const char *message) {
            isolate->ThrowException(
                v8::Exception::RangeError(v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
        }

        /**
         * Create a V8 string.
         */
        static v8::Local<v8::String> NewString(v8::Isolate *isolate, const char *str) {
            return v8::String::NewFromUtf8(isolate, str).ToLocalChecked();
        }

        /**
         * Create a V8 string from std::string.
         */
        static v8::Local<v8::String> NewString(v8::Isolate *isolate, const std::string &str) {
            return v8::String::NewFromUtf8(isolate, str.c_str()).ToLocalChecked();
        }
    };

} // namespace Framework::Scripting::JS
