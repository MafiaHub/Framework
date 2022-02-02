/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

#include <logging/logger.h>
#include <optick.h>

namespace Framework::Scripting {
    EngineError Engine::Init(SDKRegisterCallback cb) {
        // Define the arguments to be passed to the node instance
        std::vector<std::string> argv = {"main.exe"};
        std::vector<std::string> eav  = {};
        std::vector<std::string> errors;

        // Initialize the node with the provided arguments
        int exitCode = node::InitializeNodeWithArgs(&argv, &eav, &errors);
        if (exitCode != 0) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize node {}", exitCode);
            return Framework::Scripting::EngineError::ENGINE_NODE_INIT_FAILED;
        }

        // Create the multi isolate platform on a single thread
        _platform.reset(node::CreatePlatform(4, node::GetTracingController()));
        v8::V8::InitializePlatform(_platform.get());

        if (!v8::V8::Initialize()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize the V8 engine");
            return Framework::Scripting::EngineError::ENGINE_V8_INIT_FAILED;
        }

        // Create the isolate instance
        _isolate = v8::Isolate::Allocate();
        if (!_isolate) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize the node isolate");
            return Framework::Scripting::EngineError::ENGINE_ISOLATE_ALLOCATION_FAILED;
        }
        _platform->RegisterIsolate(_isolate, uv_default_loop());

        v8::Isolate::CreateParams params;
        params.array_buffer_allocator = node::CreateArrayBufferAllocator();
        v8::Isolate::Initialize(_isolate, params);

        // Initialize the error reporting and console delegate
        _isolate->SetFatalErrorHandler([](const char *location, const char *message) {
            if (location) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("{} {}", location, message);
            }
            else {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("{}", message);
            }
        });
        _isolate->AddMessageListener([](v8::Local<v8::Message> message, v8::Local<v8::Value> error) {
            auto isolate = v8::Isolate::GetCurrent();
            v8::HandleScope scope(isolate);

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Context::Scope context_scope(context);

            v8::String::Utf8Value res(isolate, message->GetScriptResourceName());
            v8::String::Utf8Value msg(isolate, message->Get());

            v8::Local<v8::Object> err_obj    = error->ToObject(context).ToLocalChecked();
            v8::Local<v8::Value> trace_value = err_obj->Get(context, v8::String::NewFromUtf8(isolate, "stack").ToLocalChecked()).ToLocalChecked();
            v8::String::Utf8Value trace(isolate, trace_value->ToString(context).ToLocalChecked());

            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("{}: {}", *res, *msg);
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("{}", *trace);
        });

        _isolate->SetCaptureStackTraceForUncaughtExceptions(true);

        v8::HandleScope isolateHandleScope(_isolate);
        v8::Local<v8::ObjectTemplate> globalObjTemplate = v8::ObjectTemplate::New(_isolate);
        _globalObjectTemplate.Reset(_isolate, globalObjTemplate);

        // Load the resources
        _resourceManager = new ResourceManager(this);

        {
            v8::Locker locker(_isolate);
            v8::Isolate::Scope isolateScope(_isolate);
            v8::HandleScope handlerScope(_isolate);
            _resourceManager->LoadAll(cb);
        }
        return Framework::Scripting::EngineError::ENGINE_NONE;
    }

    EngineError Engine::Shutdown() {
        if (!_platform || !_isolate) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to acquire platform or isolate");
            return Framework::Scripting::EngineError::ENGINE_ISOLATE_NULL;
        }

        // Acquire resources
        v8::Locker lock(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);

        // Unload the resources
        if (_resourceManager) {
            _resourceManager->UnloadAll();
        }

#ifdef WIN32
        v8::V8::Dispose();
        v8::V8::ShutdownPlatform();
#else
        _platform->DrainTasks(_isolate);
        _platform->CancelPendingDelayedTasks(_isolate);
        _platform->UnregisterIsolate(_isolate);

        _isolate->Dispose();
        v8::V8::Dispose();
        _platform.release();
#endif

        _platform.reset();
        delete _resourceManager;

        _isolate         = nullptr;
        _platform        = nullptr;
        _resourceManager = nullptr;

        return Framework::Scripting::EngineError::ENGINE_NONE;
    }

    void Engine::Update() {
        if (!_platform || !_isolate) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to acquire platform or isolate");
            return;
        }

        OPTICK_EVENT();

        // Lock and isolate
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::SealHandleScope seal(_isolate);

        // Tick the platform
        _platform->DrainTasks(_isolate);

        // Tick all resources
        for (auto resource : _resourceManager->GetAllResources()) { resource.second->Update(); }
    }
} // namespace Framework::Scripting
