/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "node_runtime.h"
#include "callback_scope.h"

namespace {
    // Node's own scope on the runtime's environment, as Node opens around its callbacks and the platform's tasks:
    // closing it runs the nextTick queue, the microtasks and the unhandled rejections. Node keeps the resource object
    // by handle, so the caller's handle scope must outlive this, which every entry's does.
    class NodeCallbackScope final: public Framework::Scripting::CallbackScope {
      public:
        NodeCallbackScope(node::Environment *env, v8::Isolate *isolate): _scope(env, v8::Object::New(isolate), {0, 0}) {}

      private:
        node::CallbackScope _scope;
    };
} // anonymous namespace

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

        // From here every entry into the runtime (a script, an event handler, another runtime's call) runs inside
        // Node's scope, as its own callbacks do.
        RegisterCallbackScope(runtime->_isolate, [env = runtime->_env, isolate = runtime->_isolate] {
            return std::make_unique<NodeCallbackScope>(env, isolate);
        });

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

        UnregisterCallbackScope(_isolate);

        // Per embedtest.cc: stop with no V8 scope held, then let CommonEnvironmentSetup free the
        // environment (which stops its workers), dispose the isolate and close the loop.
        node::Stop(_env);
        _env     = nullptr;
        _isolate = nullptr;
        _setup.reset();
    }

    v8::Local<v8::Context> NodeRuntime::GetContext() const {
        return _setup->context();
    }

    void NodeRuntime::Tick() {
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);
        v8::Context::Scope contextScope(_setup->context());

        // Run what the calls since the last tick left queued
        DrainTaskQueues();

        // Run pending libuv events (non-blocking)
        uv_run(_setup->event_loop(), UV_RUN_NOWAIT);

        // Run this isolate's foreground platform tasks. Not DrainTasks(): it first waits for every background task in
        // the process, so with a runtime per resource each tick would block on all their GC and compile work.
        _platform->FlushForegroundTasks(_isolate);

        // Node's own callbacks and the platform's tasks drain on their way out; this catches what native code with no
        // scope of its own queued meanwhile, such as an addon settling a promise from a libuv callback
        DrainTaskQueues();
    }

    void NodeRuntime::DrainTaskQueues() {
        // A call that entered through raw V8, past the framework's entries, ran with no Node callback scope, so the
        // process.nextTick callbacks it queued wait for one to close; a microtask checkpoint alone never runs them.
        // Closing this empty scope runs the nextTick queue, the microtasks and pending unhandled rejections.
        node::CallbackScope scope(_env, v8::Object::New(_isolate), {0, 0});
    }

    void NodeRuntime::RunLoop() {
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);
        v8::Context::Scope contextScope(_setup->context());
        uv_run(_setup->event_loop(), UV_RUN_NOWAIT);
    }

    int NodeRuntime::GetBackendFd() const {
        return uv_backend_fd(_setup->event_loop());
    }

    bool NodeRuntime::IsAlive() const {
        return uv_loop_alive(_setup->event_loop()) != 0;
    }

    int64_t NodeRuntime::MsUntilDue() {
        uv_loop_t *loop = _setup->event_loop();
        if (!uv_loop_alive(loop)) {
            return -1;
        }
        // Counted on the loop's own clock, which uv_run reads to decide which timers are due.
        uv_update_time(loop);
        return uv_backend_timeout(loop);
    }

} // namespace Framework::Scripting
