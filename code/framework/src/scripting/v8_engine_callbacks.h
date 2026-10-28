/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// V8 callbacks for V8Engine global APIs (require, timers, microtasks).
// Separated from v8_engine.cpp to keep engine class logic readable.
// Included only by v8_engine.cpp — not part of the public API.

#include "engine_helpers.h"
#include "resource/resource_manager.h"
#include "v8_engine.h"

#include <logging/logger.h>
#include <utils/vfs.h>

#include <fstream>
#include <memory>
#include <string>

namespace Framework::Scripting::V8EngineCallbacks {

    // Maximum size for a require()'d module file (10 MB)
    static constexpr std::streamsize kMaxModuleFileSize = 10 * 1024 * 1024;

    // Reads an entire file into a string (with size limit)
    inline bool ReadFileContents(const std::string &filepath, std::string &outContent) {
        if (!Utils::Vfs::Get().Read(filepath, outContent)) {
            return false;
        }
        if (outContent.size() > static_cast<size_t>(kMaxModuleFileSize)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Module file too large ({} bytes, max {}): {}", outContent.size(), static_cast<long long>(kMaxModuleFileSize), filepath);
            outContent.clear();
            return false;
        }
        return true;
    }

    // V8 callback: promise rejection handler
    inline void PromiseRejectCallback(v8::PromiseRejectMessage message) {
        if (auto *engine = V8Engine::FromContext(v8::Isolate::GetCurrent()->GetCurrentContext())) {
            engine->OnPromiseReject(message);
        }
    }

    // Keep origin on promises that outlive their creating callback.
    inline void PromiseHook(v8::PromiseHookType type, v8::Local<v8::Promise> promise, v8::Local<v8::Value> parent) {
        if (type != v8::PromiseHookType::kInit)
            return;
        auto *isolate = v8::Isolate::GetCurrent();
        auto context  = isolate->GetCurrentContext();
        auto *engine  = V8Engine::FromContext(context);
        auto *manager = engine ? engine->GetResourceManager() : nullptr;
        if (!manager)
            return;
        auto key          = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "Framework.promiseOwner"));
        std::string owner = manager->GetResourceContextFromStack(isolate);
        if (owner.empty())
            owner = manager->GetCurrentResourceContext();
        v8::Local<v8::Value> value;
        if (!owner.empty()) {
            value = v8::String::NewFromUtf8(isolate, owner.c_str()).ToLocalChecked();
        }
        else if (parent->IsPromise()) {
            if (!parent.As<v8::Promise>()->GetPrivate(context, key).ToLocal(&value))
                return;
        }
        if (!value.IsEmpty() && value->IsString()) {
            (void)promise->SetPrivate(context, key, value).FromMaybe(false);
            auto generationKey = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "Framework.promiseGeneration"));
            v8::Local<v8::Value> generation;
            if (owner.empty() && parent->IsPromise()) {
                if (!parent.As<v8::Promise>()->GetPrivate(context, generationKey).ToLocal(&generation))
                    return;
            }
            else {
                generation = v8::Integer::NewFromUnsigned(isolate, engine->GetResourceGeneration(owner));
            }
            if (!generation.IsEmpty())
                (void)promise->SetPrivate(context, generationKey, generation).FromMaybe(false);
        }
    }

    // V8 callback: require(specifier)
    inline void RequireCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (args.Length() < 1 || !args[0]->IsString()) {
            ThrowTypeError(isolate, "require() expects a string argument");
            return;
        }

        v8::String::Utf8Value specifier(isolate, args[0]);
        if (!*specifier) {
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "require() invalid specifier")));
            return;
        }

        std::string requested(*specifier);

        // Allow relative paths (./ or ../) — always OK
        // Allow absolute paths — only if moduleRootPath is set (validated in ResolveModulePath)
        // Reject bare specifiers (no ./ ../ or / prefix) — no Node.js built-in modules
        bool isRelative = requested.starts_with("./") || requested.starts_with("../");
        bool isAbsolute = !requested.empty() && requested[0] == '/';
#ifdef _WIN32
        if (!isAbsolute && requested.size() >= 2 && std::isalpha(static_cast<unsigned char>(requested[0])) && requested[1] == ':') {
            isAbsolute = true;
        }
#endif
        if (!isRelative && !isAbsolute) {
            std::string msg = "Cannot require bare specifier '" + requested + "'. Only relative (./ or ../) and absolute paths are supported.";
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, msg.c_str()).ToLocalChecked()));
            return;
        }

        // Extract engine and currentDir from External data
        v8::Local<v8::External> external = v8::Local<v8::External>::Cast(args.Data());
        auto *data                       = static_cast<V8Engine::RequireData *>(external->Value());

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
            ThrowTypeError(isolate, "First argument must be a function");
            return;
        }

        v8::Local<v8::External> external = v8::Local<v8::External>::Cast(args.Data());
        auto *data                       = static_cast<V8Engine::TimerCallbackData *>(external->Value());

        int delayMs = 0;
        if (args.Length() >= 2 && args[1]->IsNumber()) {
            delayMs = static_cast<int>(args[1]->NumberValue(isolate->GetCurrentContext()).FromMaybe(0));
            if (delayMs < 0)
                delayMs = 0;
        }

        v8::Local<v8::Function> callbackFn = v8::Local<v8::Function>::Cast(args[0]);

        auto entry = std::make_unique<V8Engine::TimerEntry>();
        entry->callback.Reset(isolate, callbackFn);
        entry->fireTime   = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
        entry->intervalMs = data->isInterval ? (delayMs > 0 ? delayMs : 1) : 0;
        entry->cancelled  = false;

        // Owner for cleanup on stop: callback script origin, then context, then stack.
        if (auto *mgr = data->engine->GetResourceManager()) {
            std::string owner = mgr->GetResourceNameFromFunction(isolate, callbackFn);
            if (owner.empty()) {
                owner = mgr->GetCurrentResourceContext();
            }
            if (owner.empty()) {
                owner = mgr->GetResourceContextFromStack(isolate);
            }
            entry->ownerResource = std::move(owner);
        }

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
        auto *engine                     = static_cast<V8Engine *>(external->Value());

        uint32_t timerId = args[0]->Uint32Value(isolate->GetCurrentContext()).FromMaybe(0);
        engine->CancelTimer(timerId);
    }

    // V8 callback: queueMicrotask(fn)
    inline void QueueMicrotaskCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();

        if (args.Length() < 1 || !args[0]->IsFunction()) {
            ThrowTypeError(isolate, "queueMicrotask() expects a function argument");
            return;
        }

        isolate->EnqueueMicrotask(v8::Local<v8::Function>::Cast(args[0]));
    }

} // namespace Framework::Scripting::V8EngineCallbacks
