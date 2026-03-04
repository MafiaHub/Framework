#include "engine.h"
#include "engine_helpers.h"

#include <logging/logger.h>

namespace Framework::Scripting {

    bool Engine::Execute(std::string_view code, std::string_view filename) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }

        v8::Isolate *isolate = GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handle_scope(isolate);
        v8::Local<v8::Context> context = GetContext();
        v8::Context::Scope context_scope(context);

        v8::TryCatch tryCatch(isolate);

        v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, code.data(), v8::NewStringType::kNormal, static_cast<int>(code.size())).ToLocalChecked();
        v8::ScriptOrigin origin(v8::String::NewFromUtf8(isolate, filename.data(), v8::NewStringType::kNormal, static_cast<int>(filename.size())).ToLocalChecked());

        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
            if (tryCatch.HasCaught()) {
                _lastError = FormatV8Exception(isolate, tryCatch, "Script compilation error");
            } else {
                _lastError = "Failed to compile script";
            }
            return false;
        }

        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
            if (tryCatch.HasCaught()) {
                _lastError = FormatV8Exception(isolate, tryCatch, "Runtime error");
            } else {
                _lastError = "Unknown execution error";
            }
            return false;
        }

        return true;
    }

    bool Engine::InitFrameworkSDK() {
        if (_sdkRegisterCallback) {
            _sdkRegisterCallback(this);
        }
        return true;
    }

    v8::Local<v8::Object> Engine::GetFrameworkObject() const {
        v8::Local<v8::Context> context = GetContext();
        if (context.IsEmpty()) {
            return v8::Local<v8::Object>();
        }
        v8::Isolate *isolate = GetIsolate();
        v8::Local<v8::Value> frameworkValue;
        if (!context->Global()
                ->Get(context, v8::String::NewFromUtf8(isolate, "Framework").ToLocalChecked())
                .ToLocal(&frameworkValue) ||
            !frameworkValue->IsObject()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("globalThis.Framework is missing or not an object");
            return v8::Local<v8::Object>();
        }
        return frameworkValue.As<v8::Object>();
    }

    v8::Local<v8::Object> Engine::GetCoreObject() const {
        v8::Local<v8::Context> context = GetContext();
        if (context.IsEmpty()) {
            return v8::Local<v8::Object>();
        }
        v8::Isolate *isolate = GetIsolate();
        v8::Local<v8::Value> coreValue;
        if (!context->Global()
                ->Get(context, v8::String::NewFromUtf8(isolate, "Core").ToLocalChecked())
                .ToLocal(&coreValue) ||
            !coreValue->IsObject()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("globalThis.Core is missing or not an object");
            return v8::Local<v8::Object>();
        }
        return coreValue.As<v8::Object>();
    }

} // namespace Framework::Scripting
