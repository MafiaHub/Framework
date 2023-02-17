/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <logging/logger.h>

#include <uv.h>

#include "engine.h"

namespace Framework::Scripting::Engines::Node {
    EngineError Engine::Init(SDKRegisterCallback cb) {
        // Define the arguments to be passed to the node instance
        std::vector<std::string> args = {"mafiahub-server", "--experimental-specifier-resolution=node", "--trace-warnings"};
        std::vector<std::string> exec_args {};
        std::vector<std::string> errors {};

        // Initialize the node with the provided arguments
        int initCode = node::InitializeNodeWithArgs(&args, &exec_args, &errors);
        if (initCode != 0) {
            for (std::string &error : errors) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize node: {}", error);
            }
            return Framework::Scripting::EngineError::ENGINE_NODE_INIT_FAILED;
        }

        // Initialize the platform
        _platform = node::MultiIsolatePlatform::Create(4);
        v8::V8::InitializePlatform(_platform.get());
        v8::V8::Initialize();

        // Allocate the isolate
        _isolate = v8::Isolate::Allocate();

        // Register the isolate to the platform
        _platform->RegisterIsolate(_isolate, uv_default_loop());

        // Initialize the isolate
        v8::Isolate::CreateParams params;
        params.array_buffer_allocator = node::CreateArrayBufferAllocator();
        v8::Isolate::Initialize(_isolate, params);

        // Register the IsWorker data slot
        _isolate->SetData(v8::Isolate::GetNumberOfDataSlots() - 1, new bool(false));

        // Allocate our scopes
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);

        // Create our global object template
        v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(_isolate);

        // Initialize our SDK and bind to the global object template
        _sdk = new SDK;
        _sdk->Init(_isolate, cb);
        global->Set(v8pp::to_v8(_isolate, "sdk"), _sdk->GetObjectTemplate());

        // Reset and save the global object template pointer
        _globalObjectTemplate.Reset(_isolate, global);

        // Ye
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Node.JS engine initialized!");
        return EngineError::ENGINE_NONE;
    }

    EngineError Engine::Shutdown() {
        if (!_platform) {
            return EngineError::ENGINE_PLATFORM_NULL;
        }

        if (!_isolate) {
            return EngineError::ENGINE_ISOLATE_NULL;
        }

        v8::SealHandleScope seal(_isolate);
        // Drain the remaining tasks while there are available ones
        do {
            uv_run(uv_default_loop(), UV_RUN_DEFAULT);
            _platform->DrainTasks(_isolate);
        } while (uv_loop_alive(uv_default_loop()));

        // Unregister the isolate from the platform
        _platform->UnregisterIsolate(_isolate);

        // Release the isolate
        _isolate->Dispose();

        // Release node and v8 engines
        node::FreePlatform(_platform.release());
        v8::V8::Dispose();
        v8::V8::ShutdownPlatform();

        return EngineError::ENGINE_NONE;
    }

    void Engine::Update() {
        if (!_platform) {
            return;
        }

        if (!_isolate) {
            return;
        }

        // Update the scripting layer
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::SealHandleScope seal(_isolate);
        _platform->DrainTasks(_isolate);
    }

    Resource *Engine::LoadResource(std::string fullPath) {
        const auto res = new Resource(this, _isolate, fullPath);
        return res;
    }

    Resource *Engine::UnloadResource(std::string fullPath) {
        // TODO: fix me
        return nullptr;
    }
} // namespace Framework::Scripting::Engines::Node
