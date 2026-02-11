#pragma once

// V8 callbacks for V8Engine global APIs (require, timers, microtasks).
// Separated from v8_engine.cpp to keep engine class logic readable.
// Included only by v8_engine.cpp — not part of the public API.

#include "v8_engine.h"

#include <logging/logger.h>

#include <fstream>
#include <memory>
#include <string>

namespace Framework::Scripting::V8EngineCallbacks {

    // Maximum size for a require()'d module file (10 MB)
    static constexpr std::streamsize kMaxModuleFileSize = 10 * 1024 * 1024;

    // Reads an entire file into a string (with size limit)
    inline bool ReadFileContents(const std::string &filepath, std::string &outContent) {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return false;
        }
        auto size = file.tellg();
        if (size < 0 || size > kMaxModuleFileSize) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(
                "Module file too large ({} bytes, max {}): {}",
                static_cast<long long>(size), static_cast<long long>(kMaxModuleFileSize), filepath);
            return false;
        }
        file.seekg(0, std::ios::beg);
        outContent.resize(static_cast<size_t>(size));
        file.read(outContent.data(), size);
        if (file.fail() || file.gcount() != size) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(
                "Failed to read module file (read {} of {} bytes): {}",
                static_cast<long long>(file.gcount()), static_cast<long long>(size), filepath);
            outContent.clear();
            return false;
        }
        return true;
    }

    // V8 callback: promise rejection handler
    inline void PromiseRejectCallback(v8::PromiseRejectMessage message) {
        if (message.GetEvent() == v8::kPromiseRejectWithNoHandler) {
            v8::Isolate *isolate = message.GetPromise()->GetIsolate();
            v8::Local<v8::Value> value = message.GetValue();
            if (!value.IsEmpty()) {
                v8::String::Utf8Value str(isolate, value);
                if (*str) {
                    Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Unhandled promise rejection: {}", *str);
                }
            } else {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Unhandled promise rejection (no value)");
            }
        }
    }

    // V8 callback: require(specifier)
    inline void RequireCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 1 || !args[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(isolate, "require() expects a string argument")));
            return;
        }

        v8::String::Utf8Value specifier(isolate, args[0]);
        if (!*specifier) {
            isolate->ThrowException(v8::Exception::Error(
                v8::String::NewFromUtf8Literal(isolate, "require() invalid specifier")));
            return;
        }

        std::string requested(*specifier);

        // Allow relative paths (./ or ../) — always OK
        // Allow absolute paths — only if moduleRootPath is set (validated in ResolveModulePath)
        // Reject bare specifiers (no ./ ../ or / prefix) — no Node.js built-in modules
        bool isRelative = requested.starts_with("./") || requested.starts_with("../");
        bool isAbsolute = !requested.empty() && requested[0] == '/';
#ifdef _WIN32
        if (!isAbsolute && requested.size() >= 2 &&
            std::isalpha(static_cast<unsigned char>(requested[0])) && requested[1] == ':') {
            isAbsolute = true;
        }
#endif
        if (!isRelative && !isAbsolute) {
            std::string msg = "Cannot require bare specifier '" + requested +
                "'. Only relative (./ or ../) and absolute paths are supported.";
            isolate->ThrowException(v8::Exception::Error(
                v8::String::NewFromUtf8(isolate, msg.c_str()).ToLocalChecked()));
            return;
        }

        // Extract engine and currentDir from External data
        v8::Local<v8::External> external = v8::Local<v8::External>::Cast(args.Data());
        auto *data = static_cast<V8Engine::RequireData *>(external->Value());

        v8::MaybeLocal<v8::Value> result = data->engine->LoadModule(requested, data->currentDir);
        v8::Local<v8::Value> localResult;
        if (result.ToLocal(&localResult)) {
            args.GetReturnValue().Set(localResult);
        }
        // If LoadModule failed, it already threw an exception
    }

    // V8 callback: setTimeout / setInterval
    inline void SetTimerCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 1 || !args[0]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(isolate, "First argument must be a function")));
            return;
        }

        v8::Local<v8::External> external = v8::Local<v8::External>::Cast(args.Data());
        auto *data = static_cast<V8Engine::TimerCallbackData *>(external->Value());

        int delayMs = 0;
        if (args.Length() >= 2 && args[1]->IsNumber()) {
            delayMs = static_cast<int>(args[1]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0));
            if (delayMs < 0) delayMs = 0;
        }

        auto entry = std::make_unique<V8Engine::TimerEntry>();
        entry->callback.Reset(isolate, v8::Local<v8::Function>::Cast(args[0]));
        entry->fireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
        entry->intervalMs = data->isInterval ? (delayMs > 0 ? delayMs : 1) : 0;
        entry->cancelled = false;

        // Capture extra arguments
        for (int i = 2; i < args.Length(); ++i) {
            v8::Global<v8::Value> arg;
            arg.Reset(isolate, args[i]);
            entry->args.push_back(std::move(arg));
        }

        uint32_t timerId = data->engine->AddTimer(std::move(entry));
        args.GetReturnValue().Set(v8::Integer::NewFromUnsigned(isolate, timerId));
    }

    // V8 callback: clearTimeout / clearInterval
    inline void ClearTimerCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        if (args.Length() < 1 || !args[0]->IsNumber()) {
            return; // Silently ignore invalid calls (matches browser behavior)
        }

        v8::Local<v8::External> external = v8::Local<v8::External>::Cast(args.Data());
        auto *engine = static_cast<V8Engine *>(external->Value());

        uint32_t timerId = args[0]->Uint32Value(isolate->GetCurrentContext()).FromMaybe(0);
        engine->CancelTimer(timerId);
    }

    // V8 callback: queueMicrotask(fn)
    inline void QueueMicrotaskCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        if (args.Length() < 1 || !args[0]->IsFunction()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(isolate, "queueMicrotask() expects a function argument")));
            return;
        }

        isolate->EnqueueMicrotask(v8::Local<v8::Function>::Cast(args[0]));
    }

} // namespace Framework::Scripting::V8EngineCallbacks
