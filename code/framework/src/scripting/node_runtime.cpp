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

#include <algorithm>
#include <unordered_map>

namespace {
    // Node's own scope on the runtime's environment, as Node opens around its callbacks and the platform's tasks:
    // closing it runs the nextTick queue, the microtasks and the unhandled rejections. Node keeps the resource object
    // by handle, so the caller's handle scope must outlive this, which every entry's does.
    class NodeCallbackScope final: public Framework::Scripting::CallbackScope {
      public:
        NodeCallbackScope(node::Environment *env, v8::Isolate *isolate, Framework::Scripting::NodeRuntime *runtime): _ran {runtime}, _scope(env, v8::Object::New(isolate), {0, 0}) {}

      private:
        // Told once the scope has closed: the microtasks it runs then are script V8 reports no completed call for.
        struct TellOnExit {
            Framework::Scripting::NodeRuntime *runtime;
            ~TellOnExit() {
                runtime->ScheduleChanged();
            }
        } _ran;
        node::CallbackScope _scope;
    };
} // anonymous namespace

namespace Framework::Scripting {
    namespace {
        // A message per pause this long or longer: shorter scavenges would flood the timeline.
        constexpr double kGCReportMs = 1.0;

        // V8 names only the isolate when a call completes. Read and written on the scripting thread alone: worker
        // threads run isolates of their own, which never register.
        std::unordered_map<v8::Isolate *, Framework::Scripting::NodeRuntime *> &RuntimesByIsolate() {
            static std::unordered_map<v8::Isolate *, Framework::Scripting::NodeRuntime *> runtimes;
            return runtimes;
        }
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

        runtime->_keepAlive = new uv_async_t();
        uv_async_init(runtime->_setup->event_loop(), runtime->_keepAlive, nullptr);

        RuntimesByIsolate()[runtime->_isolate] = runtime.get();
        runtime->_isolate->AddCallCompletedCallback(&NodeRuntime::OnCallCompleted);

        // From here every entry into the runtime (a script, an event handler, another runtime's call) runs inside
        // Node's scope, as its own callbacks do.
        RegisterCallbackScope(runtime->_isolate, [env = runtime->_env, isolate = runtime->_isolate, self = runtime.get()] {
            return std::make_unique<NodeCallbackScope>(env, isolate, self);
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

#ifdef _WIN32
        // First: the watcher waits on the loop's port, which closes with the loop.
        StopWatching();
#endif
        LeaveSchedule();
        UnregisterCallbackScope(_isolate);
        RuntimesByIsolate().erase(_isolate);

        // CommonEnvironmentSetup's teardown runs the loop until the platform lets go of the isolate, which takes at
        // least one run, and that run frees this handle. Left open, it would keep that run waiting and the loop from
        // closing.
        uv_close(reinterpret_cast<uv_handle_t *>(_keepAlive), [](uv_handle_t *handle) {
            delete reinterpret_cast<uv_async_t *>(handle);
        });
        _keepAlive = nullptr;

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

    RuntimeSample NodeRuntime::Sample() {
        RuntimeSample sample;
        sample.gcPauses  = _gcPauses;
        sample.gcPauseMs = _gcPauseMs;

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        // Nothing runs in the isolate while it is held here, so no pause is in progress: a prologue whose epilogue
        // never came would otherwise leave the pairing stuck and the pauses uncounted from then on.
        _gcType = static_cast<v8::GCType>(0);
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

        // Cleared first: script that runs in this tick sets it again, so the runtime is ticked once more for what
        // that script queued where the loop does not show it, such as an unreferenced immediate.
        _entered = false;

        // Run what the calls since the last tick left queued
        DrainTaskQueues();

        // Run pending libuv events (non-blocking): timers, I/O callbacks and setImmediate
        {
            FW_PROFILE_SCOPE_N("Scripting::Loop");
            RunLoopOnce();
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
        ScheduleChanged();
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
        RunLoopOnce();
    }

    void NodeRuntime::RunLoopOnce() {
#ifdef _WIN32
        // Taken before the run: a completion that arrives during it is the watcher's to report next.
        const bool signaled = _signaled.exchange(false, std::memory_order_acq_rel);
        if (signaled) {
            std::lock_guard lock(_schedule->_signaledMutex);
            std::erase(_schedule->_signaled, this);
        }
#endif
        uv_run(_setup->event_loop(), UV_RUN_NOWAIT);
        ScheduleChanged();
#ifdef _WIN32
        if (signaled) {
            ReleaseSemaphore(_resume, 1, nullptr);
        }
#endif
    }

    void NodeRuntime::OnCallCompleted(v8::Isolate *isolate) {
        const auto it = RuntimesByIsolate().find(isolate);
        if (it != RuntimesByIsolate().end()) {
            it->second->_entered = true;
            it->second->ScheduleChanged();
        }
    }

    int NodeRuntime::GetBackendFd() const {
        return uv_backend_fd(_setup->event_loop());
    }

    std::chrono::steady_clock::time_point NodeRuntime::DueAt() {
        if (_dueAtStale) {
            uv_loop_t *loop = _setup->event_loop();
            // Counted on the loop's own clock, which uv_run reads to decide which timers are due.
            uv_update_time(loop);
            const int ms = uv_backend_timeout(loop);
            if (ms < 0) {
                _dueAt = std::chrono::steady_clock::time_point::max();
            }
            else if (ms == 0) {
                // Not the time of asking: the caller's own reading of the clock is older, and would find this still to
                // come.
                _dueAt = std::chrono::steady_clock::time_point::min();
            }
            else {
                _dueAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
            }
            _dueAtStale = false;
        }
        return _dueAt;
    }

    void NodeRuntime::ScheduleChanged() {
        _dueAtStale = true;
        if (_schedule != nullptr && !_changeListed) {
            _changeListed = true;
            _schedule->_changed.push_back(this);
        }
    }

    void NodeRuntime::JoinSchedule(RuntimeSchedule &schedule) {
        if (_schedule != nullptr) {
            return;
        }
        _schedule = &schedule;
        schedule._runtimes.push_back(this);
        schedule._dueAt.push_back(RuntimeSchedule::Clock::time_point::min());
        ScheduleChanged();
#ifdef _WIN32
        StartWatching();
#endif
    }

    void NodeRuntime::LeaveSchedule() {
        if (_schedule == nullptr) {
            return;
        }
        const auto index = std::find(_schedule->_runtimes.begin(), _schedule->_runtimes.end(), this) - _schedule->_runtimes.begin();
        _schedule->_runtimes.erase(_schedule->_runtimes.begin() + index);
        _schedule->_dueAt.erase(_schedule->_dueAt.begin() + index);
        std::erase(_schedule->_changed, this);
#ifdef _WIN32
        {
            std::lock_guard lock(_schedule->_signaledMutex);
            std::erase(_schedule->_signaled, this);
        }
#endif
        _schedule = nullptr;
    }

    RuntimeSchedule::RuntimeSchedule() {
#ifdef _WIN32
        _wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
#endif
    }

    RuntimeSchedule::~RuntimeSchedule() {
#ifdef _WIN32
        CloseHandle(_wake);
#endif
    }

    RuntimeSchedule::Clock::time_point RuntimeSchedule::Collect(Clock::time_point now, std::vector<NodeRuntime *> &ready) {
        for (NodeRuntime *runtime : _changed) {
            runtime->_changeListed                                                             = false;
            _dueAt[std::find(_runtimes.begin(), _runtimes.end(), runtime) - _runtimes.begin()] = runtime->DueAt();
        }
        _changed.clear();

        auto next = Clock::time_point::max();
        for (size_t i = 0; i < _dueAt.size(); ++i) {
            if (_dueAt[i] <= now) {
                ready.push_back(_runtimes[i]);
            }
            else {
                next = std::min(next, _dueAt[i]);
            }
        }
#ifdef _WIN32
        std::lock_guard lock(_signaledMutex);
        for (NodeRuntime *runtime : _signaled) {
            if (std::find(ready.begin(), ready.end(), runtime) == ready.end()) {
                ready.push_back(runtime);
            }
        }
#endif
        return next;
    }

#ifdef _WIN32
    void NodeRuntime::StartWatching() {
        _resume     = CreateSemaphoreW(nullptr, 0, 1, nullptr);
        HANDLE port = _setup->event_loop()->iocp;
        HANDLE wake = _schedule->_wake;
        _watcher    = std::thread([this, port, wake] {
            SetThreadDescription(GetCurrentThread(), L"Scripting loop watcher");
            for (;;) {
                DWORD bytes            = 0;
                ULONG_PTR key          = 0;
                OVERLAPPED *overlapped = nullptr;
                const BOOL dequeued    = GetQueuedCompletionStatus(port, &bytes, &key, &overlapped, INFINITE);
                if (overlapped == nullptr) {
                    // An empty packet (StopWatching's, or a wake-up libuv ignores too), or the port itself failed.
                    if (_stopWatching.load(std::memory_order_acquire) || !dequeued) {
                        return;
                    }
                    continue;
                }
                // libuv reads a completion's status and size from its OVERLAPPED, so the packet given back is the one
                // the loop would have taken.
                PostQueuedCompletionStatus(port, bytes, key, overlapped);
                if (_stopWatching.load(std::memory_order_acquire)) {
                    return;
                }
                {
                    std::lock_guard lock(_schedule->_signaledMutex);
                    _schedule->_signaled.push_back(this);
                }
                _signaled.store(true, std::memory_order_release);
                // Signalling and parking in one step leaves this thread waiting by the time the loop runs. A port admits
                // one running thread at a time (libuv creates it so), and a watcher still running at that moment would
                // keep the loop's own dequeue from seeing the packet (electron/electron#14319).
                SignalObjectAndWait(wake, _resume, INFINITE, FALSE);
                if (_stopWatching.load(std::memory_order_acquire)) {
                    return;
                }
            }
        });
    }

    void NodeRuntime::StopWatching() {
        if (!_watcher.joinable()) {
            return;
        }
        _stopWatching.store(true, std::memory_order_release);
        PostQueuedCompletionStatus(_setup->event_loop()->iocp, 0, 0, nullptr);
        ReleaseSemaphore(_resume, 1, nullptr);
        _watcher.join();
        CloseHandle(_resume);
        _resume = nullptr;
    }
#endif

} // namespace Framework::Scripting
