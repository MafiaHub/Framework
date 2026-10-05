/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "node_runtime.h"

namespace Framework::Scripting {

    std::unique_ptr<NodeRuntime> NodeRuntime::Create(node::MultiIsolatePlatform *platform, const std::vector<std::string> &args, const std::vector<std::string> &execArgs, node::EnvironmentFlags::Flags flags, std::string_view bootstrap, std::string &error) {
        // CommonEnvironmentSetup gives every runtime a fresh isolate and its own uv loop.
        std::vector<std::string> errors;
        auto setup = node::CommonEnvironmentSetup::Create(platform, &errors, args, execArgs, flags);
        if (!setup) {
            error = "Failed to create Node.js environment setup";
            for (const auto &err : errors) {
                error += "\n" + err;
            }
            return nullptr;
        }

        std::unique_ptr<NodeRuntime> runtime(new NodeRuntime());
        runtime->_platform = platform;
        runtime->_isolate  = setup->isolate();
        runtime->_env      = setup->env();
        runtime->_setup    = std::move(setup);

        v8::Locker locker(runtime->_isolate);
        v8::Isolate::Scope isolateScope(runtime->_isolate);
        v8::HandleScope handleScope(runtime->_isolate);
        v8::Context::Scope contextScope(runtime->_setup->context());

        const std::string source(bootstrap);
        if (node::LoadEnvironment(runtime->_env, source.c_str()).IsEmpty()) {
            error = "Failed to load Node.js environment";
            return nullptr;
        }

        return runtime;
    }

    NodeRuntime::~NodeRuntime() {
        if (!_setup) {
            return;
        }

        // Per embedtest.cc: stop with no V8 scope held, then let CommonEnvironmentSetup free the
        // environment (which stops its workers), dispose the isolate and close the loop.
        node::Stop(_env);
        _env     = nullptr;
        _isolate = nullptr;
        _setup.reset();
    }

    uv_loop_t *NodeRuntime::GetEventLoop() const {
        return _setup->event_loop();
    }

    v8::Local<v8::Context> NodeRuntime::GetContext() const {
        return _setup->context();
    }

    void NodeRuntime::Tick() {
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);
        v8::Context::Scope contextScope(_setup->context());

        // Process microtasks (Promise continuations, async/await)
        _isolate->PerformMicrotaskCheckpoint();

        // Run pending libuv events (non-blocking)
        uv_run(_setup->event_loop(), UV_RUN_NOWAIT);

        // Drain V8 platform tasks (background compile, etc.)
        _platform->DrainTasks(_isolate);

        // Process any microtasks that were queued by I/O callbacks or platform tasks
        _isolate->PerformMicrotaskCheckpoint();
    }

} // namespace Framework::Scripting
