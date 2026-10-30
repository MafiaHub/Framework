/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "node_runtime.h"

#include <utils/profiler.h>

#include <fmt/format.h>

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

    void NodeRuntime::OnGCPrologue(v8::Isolate *, v8::GCType, v8::GCCallbackFlags, void *data) {
        static_cast<NodeRuntime *>(data)->_gcStart = std::chrono::steady_clock::now();
    }

    void NodeRuntime::OnGCEpilogue(v8::Isolate *isolate, v8::GCType type, v8::GCCallbackFlags, void *data) {
        auto *runtime   = static_cast<NodeRuntime *>(data);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - runtime->_gcStart).count();
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

        // Process microtasks (Promise continuations, async/await)
        {
            FW_PROFILE_SCOPE_N("Scripting::Microtasks");
            _isolate->PerformMicrotaskCheckpoint();
        }

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

        // Process any microtasks that were queued by I/O callbacks or platform tasks
        {
            FW_PROFILE_SCOPE_N("Scripting::Microtasks");
            _isolate->PerformMicrotaskCheckpoint();
        }
    }

} // namespace Framework::Scripting
