/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "node_runtime.h"
#include "callback_scope.h"

#include <utils/profiler.h>

#include <fmt/format.h>

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
    namespace {
        // A message per pause this long or longer: shorter scavenges would flood the timeline.
        constexpr double kGCReportMs = 1.0;
    } // namespace

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

        // Every runtime pauses its own heap on this thread, so a long pause is a server tick lost to one resource.
        // The runtime outlives its isolate's last collection: the destructor disposes the isolate inside its body.
        const auto pauses = static_cast<v8::GCType>(v8::kGCTypeScavenge | v8::kGCTypeMinorMarkSweep | v8::kGCTypeMarkSweepCompact);
        runtime->_isolate->AddGCPrologueCallback(&NodeRuntime::OnGCPrologue, runtime.get(), pauses);
        runtime->_isolate->AddGCEpilogueCallback(&NodeRuntime::OnGCEpilogue, runtime.get(), pauses);

        const std::string source(bootstrap);
        if (node::LoadEnvironment(runtime->_env, source.c_str()).IsEmpty()) {
            error = "Failed to load Node.js environment";
            return nullptr;
        }

        return runtime;
    }

    // The filter above leaves only the atomic pauses, which V8 14.6 never nests; the type check is the same
    // insurance Node's perf_hooks keep (nodejs/node#44046), for a V8 that interleaves another pair inside one.
    void NodeRuntime::OnGCPrologue(v8::Isolate *, v8::GCType type, v8::GCCallbackFlags, void *data) {
        auto *runtime = static_cast<NodeRuntime *>(data);
        if (runtime->_gcType != 0) {
            return;
        }
        runtime->_gcType  = type;
        runtime->_gcStart = std::chrono::steady_clock::now();
    }

    void NodeRuntime::OnGCEpilogue(v8::Isolate *isolate, v8::GCType type, v8::GCCallbackFlags, void *data) {
        auto *runtime = static_cast<NodeRuntime *>(data);
        if (type != runtime->_gcType) {
            return;
        }
        runtime->_gcType = static_cast<v8::GCType>(0);
        const double ms  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - runtime->_gcStart).count();
        ++runtime->_gcPauses;
        runtime->_gcPauseMs += ms;
        if (ms < kGCReportMs || !FW_PROFILE_CONNECTED()) {
            return;
        }
        v8::HeapStatistics heap;
        isolate->GetHeapStatistics(&heap);
        const char *kind          = (type & v8::kGCTypeMarkSweepCompact) ? "mark-compact" : ((type & v8::kGCTypeMinorMarkSweep) ? "minor mark-sweep" : "scavenge");
        const std::string message = fmt::format("GC {} {:.1f} ms in {} (heap {} MB)", kind, ms, runtime->_name, heap.used_heap_size() >> 20);
        FW_PROFILE_MESSAGE(message.c_str(), message.size());
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

    RuntimeSample NodeRuntime::Sample() const {
        RuntimeSample sample;
        sample.gcPauses  = _gcPauses;
        sample.gcPauseMs = _gcPauseMs;

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HeapStatistics heap;
        _isolate->GetHeapStatistics(&heap);
        sample.heapUsedBytes  = heap.used_heap_size();
        sample.heapTotalBytes = heap.total_heap_size();
        sample.heapLimitBytes = heap.heap_size_limit();
        sample.externalBytes  = heap.external_memory();
        return sample;
    }

    void NodeRuntime::Tick() {
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);
        v8::Context::Scope contextScope(_setup->context());

        // Run what the calls since the last tick left queued
        DrainTaskQueues();

        // Run pending libuv events (non-blocking): timers, I/O callbacks and setImmediate
        {
            FW_PROFILE_SCOPE_N("Scripting::Loop");
            uv_run(_setup->event_loop(), UV_RUN_NOWAIT);
        }

        // Run this isolate's foreground platform tasks. Not DrainTasks(): it first waits for every background task in
        // the process, so with a runtime per resource each tick would block on all their GC and compile work.
        {
            FW_PROFILE_SCOPE_N("Scripting::PlatformTasks");
            _platform->FlushForegroundTasks(_isolate);
        }

        // Node's own callbacks and the platform's tasks drain on their way out; this catches what native code with no
        // scope of its own queued meanwhile, such as an addon settling a promise from a libuv callback
        DrainTaskQueues();
    }

    void NodeRuntime::DrainTaskQueues() {
        // Declared first so the zone spans the drain, which runs as the scope below closes.
        FW_PROFILE_SCOPE_N("Scripting::TaskQueues");
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
        // Counted on the loop's own clock, which uv_run reads to decide which timers are due.
        uv_update_time(loop);
        return uv_backend_timeout(loop);
    }

} // namespace Framework::Scripting
