/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// CRITICAL: Include <cerrno> BEFORE any Node.js/libuv headers on Windows.
// Node.js/libuv headers interfere with Windows SDK errno definitions,
// causing EINVAL, ERANGE to be undefined when later headers need them.
#include <cerrno>

#include <node.h>
#include <uv.h>

#include "runtime_sample.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace Framework::Scripting {

    class NodeRuntime;

    /**
     * Which of an engine's runtimes have work, kept so that the engine finds them without asking every runtime. A
     * busy loop wakes the engine thousands of times a second, and reading each idle runtime on every wake-up cost
     * more than the loop's own work. A runtime that joins reports here when its loop has a completion, from its
     * watcher's thread, and when what it has due may have changed; everything else is the scripting thread's alone.
     */
    class RuntimeSchedule final {
      public:
        using Clock = std::chrono::steady_clock;

        RuntimeSchedule();
        ~RuntimeSchedule();

        RuntimeSchedule(const RuntimeSchedule &)            = delete;
        RuntimeSchedule &operator=(const RuntimeSchedule &) = delete;

        /**
         * Append to `ready` the runtimes with a completion on their loop or something due at `now`, and return when
         * the next of the others has something due: time_point::max() for never.
         */
        Clock::time_point Collect(Clock::time_point now, std::vector<NodeRuntime *> &ready);

#ifdef _WIN32
        // Signalled when a runtime's loop gets a completion.
        HANDLE GetWakeEvent() const {
            return _wake;
        }
#endif

      private:
        friend class NodeRuntime;

        // Side by side and dense: reading every runtime's due time is one pass over a few cache lines.
        std::vector<NodeRuntime *> _runtimes;
        std::vector<Clock::time_point> _dueAt;
        // The runtimes whose due time has to be read from their loop again.
        std::vector<NodeRuntime *> _changed;

#ifdef _WIN32
        HANDLE _wake = nullptr;
        // The runtimes whose loop has a completion to run; their watchers add to it.
        std::mutex _signaledMutex;
        std::vector<NodeRuntime *> _signaled;
#endif
    };

    /**
     * One Node.js environment: its own isolate, libuv loop, context and Node bootstrap.
     *
     * Everything a script creates - timers, sockets, worker threads - belongs to the environment
     * that created it and dies when the runtime is destroyed, because destruction frees the
     * environment (node::FreeEnvironment stops and joins its workers) and closes its loop.
     *
     * The process-wide platform is not owned here; every runtime is created against the one the
     * engine initialised, and they all run on the thread that ticks them.
     *
     * The runtime holds a referenced handle of its own on its loop, as Electron does for the loop it embeds, so the
     * loop is always alive: uv_run then runs whatever reaches it, the platform's tasks and their delayed timers
     * included, and a runtime with nothing due can be left alone until its loop says otherwise.
     */
    class NodeRuntime final {
      public:
        /**
         * Create the environment and run `bootstrap` as its main script.
         *
         * Only one environment in the process may own the inspector (Node asserts on a second
         * agent) and only one should own process state such as the working directory, so every
         * environment after the first is created with flags that claim neither.
         * @return The runtime, or null with the reason in `error`.
         */
        static std::unique_ptr<NodeRuntime> Create(node::MultiIsolatePlatform *platform, const std::vector<std::string> &args, const std::vector<std::string> &execArgs, node::EnvironmentFlags::Flags flags, std::string_view bootstrap, std::string &error);

        ~NodeRuntime();

        NodeRuntime(const NodeRuntime &)            = delete;
        NodeRuntime &operator=(const NodeRuntime &) = delete;

        v8::Isolate *GetIsolate() const {
            return _isolate;
        }

        node::Environment *GetEnvironment() const {
            return _env;
        }

        // The caller owns the Locker / Isolate::Scope / HandleScope before calling this.
        v8::Local<v8::Context> GetContext() const;

        /**
         * Run the process.nextTick queue, microtasks, ready libuv callbacks and this isolate's platform tasks
         * without blocking. Enters the isolate itself.
         *
         * A call that enters through one of the framework's entries (EngineScope, Engine::Execute, another runtime's
         * call) runs inside Node's callback scope, so what it queues through process.nextTick or a promise runs as the
         * call returns, as Node runs it. One made through raw V8 with no scope has that run here, on the next tick:
         * two such calls in a row both finish before anything either queued.
         */
        void Tick();

        /**
         * Whether a call has entered the runtime since its last Tick(): what a call made through raw V8 queued waits
         * for that tick. V8 reports every outermost call that completes, Node's own callbacks included.
         */
        bool WasEntered() const {
            return _entered;
        }

        // Names the runtime in profiler zones and GC messages (the resource, or "server").
        void SetName(std::string name) {
            _name = std::move(name);
        }

        const std::string &GetName() const {
            return _name;
        }

        // Heap figures and the collector's running totals. Locks the isolate, so nothing may be running in it.
        RuntimeSample Sample();

        /**
         * Run ready libuv callbacks once, without Tick()'s drains: Node's own callbacks drain on their way out.
         * Enters the isolate itself.
         */
        void RunLoop();

        /**
         * The fd that is readable while the loop has I/O to run, or -1 where there is none (Windows).
         */
        int GetBackendFd() const;

        /**
         * When the loop next has a timer, immediate or pending callback to run: a time not after now when it has one
         * already, time_point::max() when it has none. I/O is not counted: it is what GetBackendFd() or the watcher
         * reports. Only something running in the runtime or entering it changes the answer, so the loop is asked
         * again only after that, and asking an idle runtime costs a comparison.
         */
        std::chrono::steady_clock::time_point DueAt();

        /**
         * Whether the loop has a completion its watcher saw and gave back since the loop last ran. Always false
         * without a watcher.
         */
        bool IsSignaled() const {
#ifdef _WIN32
            return _signaled.load(std::memory_order_acquire);
#else
            return false;
#endif
        }

        /**
         * Report this runtime's work to `schedule`, which must outlive it. On Windows this also starts a thread of
         * the runtime's own that waits on the loop's completion port and signals the schedule when a completion
         * arrives, as Electron's embedding does: a port can neither be waited on together with others nor checked
         * without taking a packet, so the watcher takes it, gives it back, and stays parked until the loop has run.
         */
        void JoinSchedule(RuntimeSchedule &schedule);

        // Something ran in the runtime or entered it, so what its loop has due may have changed.
        void ScheduleChanged();

      private:
        friend class RuntimeSchedule;

        NodeRuntime() = default;

        static void OnGCPrologue(v8::Isolate *isolate, v8::GCType type, v8::GCCallbackFlags flags, void *data);
        static void OnGCEpilogue(v8::Isolate *isolate, v8::GCType type, v8::GCCallbackFlags flags, void *data);

        // The caller holds the locker and the isolate, handle and context scopes.
        void DrainTaskQueues();

        // uv_run(UV_RUN_NOWAIT), then lets the watcher wait for the loop's next completion. The caller holds the
        // locker and the isolate, handle and context scopes.
        void RunLoopOnce();

        static void OnCallCompleted(v8::Isolate *isolate);

        void LeaveSchedule();

#ifdef _WIN32
        void StartWatching();
        void StopWatching();
#endif

        node::MultiIsolatePlatform *_platform = nullptr;
        std::unique_ptr<node::CommonEnvironmentSetup> _setup;
        node::Environment *_env = nullptr;
        v8::Isolate *_isolate   = nullptr;
        std::string _name       = "starting";
        v8::GCType _gcType      = static_cast<v8::GCType>(0); // the pause in progress, or 0 outside one
        std::chrono::steady_clock::time_point _gcStart;
        uint64_t _gcPauses = 0;
        double _gcPauseMs  = 0.0;

        // Keeps the loop alive; closed before the environment is freed, whose teardown runs the loop that frees it.
        uv_async_t *_keepAlive = nullptr;
        // Starts set, so the first tick drains what the bootstrap queued.
        bool _entered = true;
        // _dueAt is what the loop last answered; stale once something ran in the runtime or entered it.
        bool _dueAtStale = true;
        std::chrono::steady_clock::time_point _dueAt;
        RuntimeSchedule *_schedule = nullptr;
        bool _changeListed         = false; // already in the schedule's list of changed runtimes

#ifdef _WIN32
        std::thread _watcher;
        HANDLE _resume = nullptr; // released once the loop has run what the watcher saw
        std::atomic<bool> _signaled {false};
        std::atomic<bool> _stopWatching {false};
#endif
    };

} // namespace Framework::Scripting
