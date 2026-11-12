/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "node_engine.h"
#include "builtins/builtins.h"
#include "builtins/messages.h"
#include "callback_scope.h"
#include "engine_helpers.h"
#include "resource/resource_manager.h"
#include "scripting_catalog.h"

#include <logging/logger.h>
#include <utils/profiler.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#else
#include <cstring>
#include <poll.h>
#endif

namespace {
    // Script started this close to the next tick could run into it.
    constexpr auto kRunMargin = std::chrono::milliseconds(1);

    // The off-thread runtime of a thread that bound none.
    constexpr std::string_view kDefaultOffThreadRuntime = "tick";

    // Which off-thread runtime this thread bound, and in which engine's set: an engine that went away, or shut down,
    // leaves a set no later one has, so what is left here of it names nothing.
    struct OffThreadBinding {
        uint64_t set = 0;
        size_t index = 0;
    };
    thread_local OffThreadBinding tOffThreadBinding;
    std::atomic<uint64_t> gNextOffThreadSet {1};

    // Escapes a string for safe embedding in a JavaScript single-quoted string literal.
    // Handles: backslash, single quote, newline, carriage return, and tab.
    std::string EscapeForSingleQuotedJSString(const std::string &input) {
        std::string result;
        result.reserve(input.size() + input.size() / 8);

        for (char c : input) {
            switch (c) {
            case '\\': result += "\\\\"; break;
            case '\'': result += "\\'"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
            }
        }
        return result;
    }

    // Node.js internals with require setup and uncaught exception/rejection
    // handlers. These prevent async errors (timers, promises) from crashing
    // the host process.
    //
    // setUncaughtExceptionCaptureCallback is preferred over process.on('uncaughtException')
    // because it:
    //   - Cannot be removed by user scripts (process.removeAllListeners)
    //   - Prevents abort even with --abort-on-uncaught-exception
    //   - Is designed for embedder use cases
    //
    // For unhandled promise rejections, process.on('unhandledRejection') is used
    // as there is no capture callback equivalent.
    //
    // Both route to __fw_handleUncaughtError if installed later via
    // InstallUncaughtExceptionHandler(), otherwise log to stderr.
    constexpr const char *kRuntimeBootstrap = "const publicRequire = require('node:module').createRequire(process.cwd() + '/');"
                                              "globalThis.require = publicRequire;"
                                              "process.setUncaughtExceptionCaptureCallback((err) => {"
                                              "  try {"
                                              "    const msg = err instanceof Error ? (err.stack || err.message) : String(err);"
                                              "    if (typeof globalThis.__fw_handleUncaughtError === 'function') {"
                                              "      globalThis.__fw_handleUncaughtError(msg, 'uncaughtException');"
                                              "    } else {"
                                              "      console.error('[uncaughtException]', msg);"
                                              "    }"
                                              "  } catch(e) {"
                                              "    console.error('Error in uncaught exception handler:', e);"
                                              "  }"
                                              "});"
                                              "process.on('unhandledRejection', (reason) => {"
                                              "  try {"
                                              "    const msg = reason instanceof Error ? (reason.stack || reason.message) : String(reason);"
                                              "    if (typeof globalThis.__fw_handleUncaughtError === 'function') {"
                                              "      globalThis.__fw_handleUncaughtError(msg, 'unhandledRejection');"
                                              "    } else {"
                                              "      console.error('[unhandledRejection]', msg);"
                                              "    }"
                                              "  } catch(e) {"
                                              "    console.error('Error in unhandled rejection handler:', e);"
                                              "  }"
                                              "});";
} // anonymous namespace

namespace Framework::Scripting {

    std::unique_ptr<node::MultiIsolatePlatform> NodeEngine::_platform   = nullptr;
    std::shared_ptr<node::InitializationResult> NodeEngine::_initResult = nullptr;
    bool NodeEngine::_platformInitialized                               = false;

    NodeEngine::NodeEngine(const NodeEngineOptions &options): _options(options) {}

    NodeEngine::~NodeEngine() {
        Shutdown();
#ifdef _WIN32
        if (_waitTimer != nullptr) {
            CloseHandle(_waitTimer);
        }
#endif
    }

    ScriptingError NodeEngine::Init() {
        _lastError.clear();

        if (_initialized) {
            return ScriptingError::SCRIPTING_NONE;
        }

        if (!InitializeNode()) {
            return ScriptingError::SCRIPTING_PLATFORM_INIT_FAILED;
        }

#ifdef _WIN32
        if (_waitTimer == nullptr) {
            _waitTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        }
#endif

        if (!CreateEnvironment()) {
            return ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
        }

        _initialized = true;
        return ScriptingError::SCRIPTING_NONE;
    }

    void NodeEngine::Shutdown() {
        if (!_initialized || !_runtime) {
            _initialized = false;
            return;
        }

        // Resources have normally stopped by now; any runtime left goes before the engine's own, including any that a
        // teardown retires along the way.
        _pendingExits.clear();
        while (!_resourceRuntimes.empty() || !_retiredRuntimes.empty()) {
            if (!_resourceRuntimes.empty()) {
                auto node = _resourceRuntimes.extract(_resourceRuntimes.begin());
                TeardownResourceRuntime(std::move(node.mapped()));
                continue;
            }
            std::unique_ptr<ResourceRuntime> runtime = std::move(_retiredRuntimes.back());
            _retiredRuntimes.pop_back();
            TeardownResourceRuntime(std::move(runtime));
        }

        // An off-thread runtime goes like a resource's: out of the references, its bindings dropped, then freed.
        for (OffThreadRuntime &entry : _offThreadRuntimes) {
            v8::Isolate *offThread = entry.runtime->GetIsolate();
            {
                v8::Locker locker(offThread);
                v8::Isolate::Scope isolateScope(offThread);
                v8::HandleScope handleScope(offThread);
                v8::Context::Scope contextScope(entry.runtime->GetContext());
                _references->RemoveRuntime(entry.reference);
            }
            Builtins::UnregisterAll(offThread);
            entry.runtime.reset();
        }
        _offThreadRuntimes.clear();
        _offThreadSet = 0;

        v8::Isolate *isolate = _runtime->GetIsolate();
        {
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Context::Scope contextScope(_runtime->GetContext());

            Builtins::Messages::Shutdown();
            _references->RemoveRuntime(_hostReference);
        }
        _references.reset();

        // Release persistent handles before destroying the isolate
        _interruptDrainFn.Reset();

        // Drop cached builtin class wrappers before the isolate dies (avoids leak + stale reuse).
        Builtins::UnregisterAll(isolate);
        _monitor.Untrack(isolate);
        if (ResourceMonitor::Current() == &_monitor) {
            ResourceMonitor::SetCurrent(nullptr);
        }

        // Stops the environment with no V8 scope held, then disposes the isolate (embedtest.cc order).
        _runtime.reset();

        _initialized = false;
    }

    bool NodeEngine::InitializeNode() {
        if (_platformInitialized) {
            return true;
        }

        // Build args from options
        std::vector<std::string> nodeArgs = {_options.processName};

#ifdef FW_NODE_INSPECTOR
        if (_options.enableInspector) {
            std::string flag = _options.inspectorWaitForDebugger ? "--inspect-brk=" : "--inspect=";
            flag += _options.inspectorHost + ":" + std::to_string(_options.inspectorPort);
            nodeArgs.push_back(flag);
        }
#endif

        // Preserve the host's shutdown/crash handlers and initialize V8 ourselves.
        _initResult = node::InitializeOncePerProcess(nodeArgs, {node::ProcessInitializationFlags::kNoInitializeV8, node::ProcessInitializationFlags::kNoInitializeNodeV8Platform, node::ProcessInitializationFlags::kNoDefaultSignalHandling});

        for (const auto &err : _initResult->errors()) {
            _lastError += err + "\n";
        }

        if (_initResult->early_return() != 0) {
            _lastError = "Failed to initialize Node.js process: " + _lastError;
            return false;
        }

        // Create MultiIsolatePlatform for Worker thread support
        _platform = node::MultiIsolatePlatform::Create(std::max(_options.platformThreads, 1));
        v8::V8::InitializePlatform(_platform.get());
        v8::V8::Initialize();

        _platformInitialized = true;

#ifdef FW_NODE_INSPECTOR
        if (_options.enableInspector) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Node.js inspector listening on {}:{}", _options.inspectorHost, _options.inspectorPort);
        }
#endif

        return true;
    }

    std::unique_ptr<NodeRuntime> NodeEngine::CreateRuntime(std::string &error) const {
        if (!_platformInitialized) {
            error = "Node.js platform not initialized";
            return nullptr;
        }
        // The engine's own environment holds the inspector and the process state. File descriptors opened through
        // fs.open() are tracked so freeing the environment closes them, as it does every other handle.
        const auto flags = static_cast<node::EnvironmentFlags::Flags>(node::EnvironmentFlags::kNoCreateInspector | node::EnvironmentFlags::kTrackUnmanagedFds);
        return NodeRuntime::Create(_platform.get(), _initResult->args(), _initResult->exec_args(), flags, kRuntimeBootstrap, error);
    }

    bool NodeEngine::CreateEnvironment() {
        _runtime = NodeRuntime::Create(_platform.get(), _initResult->args(), _initResult->exec_args(), node::EnvironmentFlags::kDefaultFlags, kRuntimeBootstrap, _lastError);
        if (!_runtime) {
            return false;
        }
        _runtime->SetName("server");
        _runtime->JoinSchedule(_schedule);
        _monitor.Track(_runtime->GetIsolate(), _runtime->GetName());
        ResourceMonitor::SetCurrent(&_monitor);

        v8::Isolate *isolate = _runtime->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Context::Scope contextScope(_runtime->GetContext());

        _references    = std::make_unique<FunctionReferences>();
        _hostReference = _references->AddRuntime(isolate, _runtime->GetContext(), "server");

        // Apply sandbox restrictions if enabled
        if (_options.sandboxed && !ApplySandbox(_runtime->GetContext())) {
            _lastError = "Failed to apply sandbox: " + _lastError;
            return false;
        }

#ifdef FW_NODE_INSPECTOR
        // Cache a JS function for inspector interrupt draining.
        // Node's internal task_queues_async_ callback is empty and its
        // CheckImmediate uv_check handle is only active when setImmediate()
        // is pending, so uv_run(UV_RUN_NOWAIT) alone cannot drain interrupts.
        // Calling setImmediate() each tick both enters JS execution (triggering
        // V8 safepoint for interrupt draining) and activates CheckImmediate
        // which calls RunAndClearNativeImmediates → RunAndClearInterrupts.
        if (_options.enableInspector) {
            v8::Local<v8::Context> ctx   = _runtime->GetContext();
            v8::Local<v8::String> source = v8::String::NewFromUtf8Literal(GetIsolate(), "(function(){ setImmediate(function(){}); })");
            v8::Local<v8::Script> script;
            if (v8::Script::Compile(ctx, source).ToLocal(&script)) {
                v8::Local<v8::Value> result;
                if (script->Run(ctx).ToLocal(&result) && result->IsFunction()) {
                    _interruptDrainFn.Reset(GetIsolate(), result.As<v8::Function>());
                }
            }
        }
#endif

        return true;
    }

    void NodeEngine::Tick() {
        if (!_initialized || !_runtime) {
            return;
        }

#ifdef FW_NODE_INSPECTOR
        // Trigger V8 interrupt processing for inspector CDP messages.
        // Calling setImmediate() enters JS (draining V8 interrupts at the
        // safepoint) and activates Node's CheckImmediate uv_check handle,
        // which calls RunAndClearNativeImmediates → RunAndClearInterrupts.
        if (!_interruptDrainFn.IsEmpty()) {
            FW_PROFILE_SCOPE_N("Scripting::InspectorDrain");
            v8::Isolate *isolate = _runtime->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _runtime->GetContext();
            v8::Context::Scope contextScope(context);
            const auto callbackScope = OpenCallbackScope(isolate);
            _interruptDrainFn.Get(isolate)->Call(context, v8::Undefined(isolate), 0, nullptr).FromMaybe(v8::Local<v8::Value>());
        }
#endif

        {
            FW_PROFILE_SCOPE_N("Scripting::RuntimeUpkeep");
            StopExitedResources();
            FlushRetiredRuntimes();
            _references->ReleaseCollected();
        }

#ifndef _WIN32
        PollReadyLoops();
#endif

        // A runtime that is executing further up this thread's stack is not ticked from inside itself: uv_run does
        // not nest.
        if (!_runtime->GetIsolate()->IsInUse() && HasWork(*_runtime)) {
            FW_PROFILE_SCOPE_N("Scripting::Runtime");
            FW_PROFILE_TEXT(_runtime->GetName().c_str(), _runtime->GetName().size());
            ResourceMonitor::Scope charged(_monitor, _runtime->GetIsolate());
            _runtime->Tick();
        }

        TickResourceRuntimes();

        StopExitedResources();
        FlushRetiredRuntimes();
    }

    void NodeEngine::CloseMonitorTick() {
        const auto now = ResourceMonitor::Clock::now();
        if (_monitor.SampleDue(now)) {
            SampleRuntimes();
        }
        _monitor.EndTick(now);
    }

    void NodeEngine::SampleRuntimes() {
        // A runtime executing further up the stack is skipped this second: its heap is read with nothing running in it.
        if (!_runtime->GetIsolate()->IsInUse()) {
            _monitor.Record(_runtime->GetIsolate(), _runtime->Sample());
        }
        for (const auto &[name, entry] : _resourceRuntimes) {
            if (!entry->exited && !entry->runtime->GetIsolate()->IsInUse()) {
                _monitor.Record(entry->runtime->GetIsolate(), entry->runtime->Sample());
            }
        }
    }

    void NodeEngine::TickResourceRuntimes() {
        for (auto it = _resourceRuntimes.begin(); it != _resourceRuntimes.end();) {
            ResourceRuntime &entry    = *it->second;
            const uint64_t generation = _runtimesGeneration;
            if (!entry.exited && !entry.runtime->GetIsolate()->IsInUse() && HasWork(*entry.runtime)) {
                FW_PROFILE_SCOPE_N("Scripting::Runtime");
                FW_PROFILE_TEXT(entry.resourceName.c_str(), entry.resourceName.size());
                v8::Isolate *isolate = entry.runtime->GetIsolate();
                const double charged = _monitor.ChargedMs(isolate, ResourceMonitor::Clock::now());
                {
                    ResourceMonitor::Scope scope(_monitor, isolate);
                    entry.runtime->Tick();
                }
                _monitor.ReportOwnTime(isolate, "timers, I/O and await continuations", {}, charged, ResourceMonitor::Clock::now());
            }
            if (generation == _runtimesGeneration) {
                ++it;
                continue;
            }
            // The tick started or stopped resources, which may have moved the map under the iterator. This entry was
            // executing, so it was at most retired, not freed: its name still says where to carry on.
            it = _resourceRuntimes.upper_bound(entry.resourceName);
        }
    }

    bool NodeEngine::HasWork(NodeRuntime &runtime) const {
        if (runtime.WasEntered() || runtime.IsSignaled() || runtime.DueAt() <= std::chrono::steady_clock::now()) {
            return true;
        }
#ifdef _WIN32
        return false;
#else
        return std::ranges::contains(_ioReady, &runtime);
#endif
    }

#ifndef _WIN32
    void NodeEngine::PollReadyLoops() {
        _ioReady.clear();
        std::vector<pollfd> fds;
        std::vector<NodeRuntime *> owners;
        const auto add = [&](NodeRuntime &runtime) {
            fds.push_back({runtime.GetBackendFd(), POLLIN, 0});
            owners.push_back(&runtime);
        };
        add(*_runtime);
        for (const auto &[name, entry] : _resourceRuntimes) {
            if (!entry->exited) {
                add(*entry->runtime);
            }
        }
        // One poll over every loop instead of a run of each; RunUntil reports a failing poll.
        const int ready = poll(fds.data(), static_cast<nfds_t>(fds.size()), 0);
        if (ready < 0) {
            // Which loops have I/O is unknown, so the tick runs them all.
            _ioReady = std::move(owners);
            return;
        }
        if (ready > 0) {
            for (size_t i = 0; i < fds.size(); ++i) {
                if (fds[i].revents != 0) {
                    _ioReady.push_back(owners[i]);
                }
            }
        }
    }
#endif

    void NodeEngine::GateRelease() {
        if (_runGate != nullptr && !_gateLost) {
            _runGate->Release();
        }
    }

    void NodeEngine::GateAcquire() {
        if (_runGate != nullptr && !_gateLost && !_runGate->Acquire()) {
            _gateLost = true;
        }
    }

    void NodeEngine::RunUntil(std::chrono::steady_clock::time_point deadline) {
        using Clock       = std::chrono::steady_clock;
        _gateLost         = false;
        const auto stopAt = deadline - kRunMargin;
        // Which runtimes have work now: a completion on their loop, or a timer, immediate or callback due. Kept across
        // rounds.
        std::vector<ResourceRuntime *> due;
#ifdef _WIN32
        while (_initialized && _runtime && _pendingErrors.empty() && !_gateLost) {
            const auto now = Clock::now();
            if (now >= stopAt) {
                break;
            }
            const uint64_t generation = _runtimesGeneration;
            auto next                 = stopAt;
            CollectReady(now, due, next);
            // Each loop's watcher signals the schedule when a completion reaches the loop's port, so the wait is for
            // that or the next timer. What woke it is found on the next round.
            if (due.empty()) {
                WaitForWork(next);
                continue;
            }
            RunReady(due, generation, stopAt);
        }
#else
        // Every loop's backend fd, readable while the loop has I/O to run.
        std::vector<ResourceRuntime *> watched;
        std::vector<pollfd> fds;
        while (_initialized && _runtime && _pendingErrors.empty() && !_gateLost && Clock::now() < stopAt) {
            const uint64_t generation = _runtimesGeneration;
            const auto now            = Clock::now();
            // How long until the next runtime has something due, if nothing has now.
            int64_t waitMs = std::max<int64_t>(0, std::chrono::ceil<std::chrono::milliseconds>(stopAt - now).count());
            due.clear();
            watched.clear();
            fds.clear();
            // Every loop's fd is polled on every wait here, so each runtime is asked in turn; one with nothing new
            // answers from what its loop last said.
            const auto consider = [&](ResourceRuntime *entry, NodeRuntime &runtime) {
                if (runtime.GetIsolate()->IsInUse()) {
                    return;
                }
                const auto dueAt = runtime.DueAt();
                if (dueAt <= now) {
                    due.push_back(entry);
                    return;
                }
                if (dueAt != Clock::time_point::max()) {
                    waitMs = std::min<int64_t>(waitMs, std::chrono::ceil<std::chrono::milliseconds>(dueAt - now).count());
                }
                watched.push_back(entry);
                fds.push_back({runtime.GetBackendFd(), POLLIN, 0});
            };
            consider(nullptr, *_runtime);
            for (const auto &[name, entry] : _resourceRuntimes) {
                if (!entry->exited) {
                    consider(entry.get(), *entry->runtime);
                }
            }

            // Wait only while nothing is ready; I/O is still picked up between rounds of a runtime that stays busy.
            const bool idle = due.empty();
            // A loop's backend fd is readable while the loop has I/O to run, and polling it consumes nothing. With no
            // fd to wait on, sleep instead: some macOS releases return from poll() at once for zero fds.
            if (fds.empty()) {
                if (idle) {
                    SleepUntil(std::min(Clock::now() + std::chrono::milliseconds(waitMs), stopAt));
                }
            }
            else {
                // Only a wait gives the world up: a poll that returns at once is not worth the handoff.
                const bool waits = idle && waitMs > 0;
                if (waits) {
                    GateRelease();
                }
                const int ready = poll(fds.data(), static_cast<nfds_t>(fds.size()), idle ? static_cast<int>(waitMs) : 0);
                if (waits) {
                    GateAcquire();
                }
                if (_gateLost) {
                    return;
                }
                if (ready > 0) {
                    for (size_t i = 0; i < fds.size(); ++i) {
                        if (fds[i].revents != 0) {
                            due.push_back(watched[i]);
                        }
                    }
                }
                else if (ready < 0 && errno != EINTR) {
                    // Nothing about the fds changes before the tick, so a poll that fails outright would fail every
                    // round: sleep the round out instead of spinning on it, and leave the I/O to the tick, as before.
                    // Timers still set the wait.
                    const int error = errno;
                    if (!_pollFailed) {
                        _pollFailed = true;
                        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("poll() over the script loops failed: {}. While it does, script I/O waits for the next tick", std::strerror(error));
                    }
                    if (idle) {
                        SleepUntil(std::min(Clock::now() + std::chrono::milliseconds(waitMs), stopAt));
                    }
                }
            }
            RunReady(due, generation, stopAt);
        }
#endif
        if (_pendingErrors.empty() && !_gateLost) {
            SleepUntil(deadline);
        }
    }

    void NodeEngine::RunReady(const std::vector<ResourceRuntime *> &ready, uint64_t generation, std::chrono::steady_clock::time_point stopAt) {
        if (ready.empty()) {
            return;
        }
        for (ResourceRuntime *entry : ready) {
            // Between two runtimes nothing of a script is on the stack: where a waiting tick, or a thread that wants
            // to run script itself, is let in. What it started or stopped shows in the generation below.
            if (_gateLost || (_runGate != nullptr && !_runGate->Checkpoint())) {
                _gateLost = true;
                return;
            }
            // Script started or stopped resources: whatever is still ready is found on the next pass.
            if (generation != _runtimesGeneration || std::chrono::steady_clock::now() >= stopAt) {
                break;
            }
            NodeRuntime &runtime = entry != nullptr ? *entry->runtime : *_runtime;
            if ((entry == nullptr || !entry->exited) && !runtime.GetIsolate()->IsInUse()) {
                FW_PROFILE_SCOPE_N("Scripting::RunLoop");
                FW_PROFILE_TEXT(runtime.GetName().c_str(), runtime.GetName().size());
                // Charged as the tick's own loop run is. A busy loop runs many times a tick, so the charge is read back
                // only when the run itself lasted the slow threshold.
                v8::Isolate *isolate = runtime.GetIsolate();
                const auto start     = ResourceMonitor::Clock::now();
                const double charged = _monitor.ChargedMs(isolate, start);
                _monitor.Enter(isolate, start);
                runtime.RunLoop();
                const auto end = ResourceMonitor::Clock::now();
                _monitor.Leave(end);
                if (_monitor.IsSlow(std::chrono::duration<double, std::milli>(end - start).count())) {
                    _monitor.ReportOwnTime(isolate, "timers, I/O and await continuations", {}, charged, end);
                }
            }
        }
        StopExitedResources();
        FlushRetiredRuntimes();
    }

    void NodeEngine::SleepUntil(std::chrono::steady_clock::time_point until) {
        if (std::chrono::steady_clock::now() >= until) {
            return;
        }
        GateRelease();
        struct Reacquire {
            NodeEngine *engine;
            ~Reacquire() {
                engine->GateAcquire();
            }
        } reacquire {this};
#ifdef _WIN32
        // The timer runs on another clock and can fire a little before `until`.
        for (auto now = std::chrono::steady_clock::now(); _waitTimer != nullptr && now < until; now = std::chrono::steady_clock::now()) {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>(std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(until - now).count() / 100));
            if (!SetWaitableTimer(_waitTimer, &due, 0, nullptr, nullptr, FALSE) || WaitForSingleObject(_waitTimer, INFINITE) != WAIT_OBJECT_0) {
                break;
            }
        }
#endif
        std::this_thread::sleep_until(until);
    }

#ifdef _WIN32
    void NodeEngine::WaitForWork(std::chrono::steady_clock::time_point until) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= until) {
            return;
        }
        GateRelease();
        struct Reacquire {
            NodeEngine *engine;
            ~Reacquire() {
                engine->GateAcquire();
            }
        } reacquire {this};
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(until - now).count() / 100));
        if (!SetWaitableTimer(_waitTimer, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(_schedule.GetWakeEvent(), static_cast<DWORD>(std::chrono::ceil<std::chrono::milliseconds>(until - now).count()));
            return;
        }
        const HANDLE handles[] = {_schedule.GetWakeEvent(), _waitTimer};
        if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_FAILED) {
            // Not a wait the caller can loop on.
            std::this_thread::sleep_until(until);
        }
    }

    void NodeEngine::CollectReady(std::chrono::steady_clock::time_point now, std::vector<ResourceRuntime *> &due, std::chrono::steady_clock::time_point &next) {
        _ready.clear();
        next = std::min(next, _schedule.Collect(now, _ready));
        due.clear();
        for (NodeRuntime *runtime : _ready) {
            ResourceRuntime *entry = nullptr;
            if (runtime != _runtime.get()) {
                const auto it = _runtimeByIsolate.find(runtime->GetIsolate());
                if (it == _runtimeByIsolate.end() || it->second->exited || it->second->retired) {
                    continue;
                }
                entry = it->second;
            }
            // One executing further up the stack cannot run its loop, and is not waited on either.
            if (!runtime->GetIsolate()->IsInUse()) {
                due.push_back(entry);
            }
        }
    }
#endif

    void NodeEngine::StopExitedResources() {
        std::vector<PendingExit> exits;
        exits.swap(_pendingExits);
        std::vector<PendingExit> later;
        for (const auto &exit : exits) {
            // A runtime the resource has since been restarted in did not exit.
            const auto it = _resourceRuntimes.find(exit.resourceName);
            if (it == _resourceRuntimes.end() || it->second->serial != exit.serial) {
                continue;
            }
            ResourceRuntime &entry = *it->second;

            // Still unwinding from the exit further up the stack: wait for a tick that starts outside it.
            if (entry.runtime->GetIsolate()->IsInUse()) {
                later.push_back(exit);
                continue;
            }

            // Cut it off first: its script is over, so calls into it throw, what it owed other resources is rejected
            // and its handlers are dropped, so a stop or start waiting on it is not left waiting on a runtime that no
            // longer runs.
            if (!entry.closed) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resource '{}' called process.exit({}); stopping the resource", exit.resourceName, exit.code);
                CloseExitedRuntime(entry);
            }

            if (_resourceManager == nullptr) {
                DestroyResourceRuntime(exit.resourceName);
                continue;
            }
            // The manager frees the runtime: through StopResource when it runs, or itself when it is starting or
            // stopping it, which the exit lets finish; then a resource that came up running is stopped here.
            switch (_resourceManager->GetResourceState(exit.resourceName)) {
            case ResourceState::Running: _resourceManager->StopResource(exit.resourceName); break;
            case ResourceState::Loading:
            case ResourceState::Stopping: later.push_back(exit); break;
            default: break;
            }
        }
        _pendingExits.insert(_pendingExits.end(), later.begin(), later.end());
    }

    void NodeEngine::CloseExitedRuntime(ResourceRuntime &runtime) {
        v8::Isolate *isolate = runtime.runtime->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        _references->RemoveRuntime(runtime.reference);
        if (_resourceManager != nullptr) {
            _resourceManager->OnRuntimeDisposing(isolate);
        }
        runtime.closed = true;
    }

    bool NodeEngine::ExecuteFile(std::string_view filepath) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }

        // Convert to absolute path for Node.js require
        // Use generic_string() to get forward slashes on all platforms
        std::filesystem::path absPath = std::filesystem::absolute(filepath);
        std::string absPathStr        = absPath.generic_string();

        // Use Node.js require for file execution
        // Escape the path for safe embedding in JS single-quoted string
        std::string escapedPath = EscapeForSingleQuotedJSString(absPathStr);
        std::string code        = "require('" + escapedPath + "');";
        return Execute(code, absPathStr);
    }

    bool NodeEngine::OffEngineThread() const {
        if (_offThreadRuntimes.empty()) {
            return false;
        }
        ScriptGate *gate = ScriptGate::Get();
        return gate != nullptr && !gate->OnScriptThread();
    }

    NodeRuntime *NodeEngine::OffThreadRuntimeFor(v8::Isolate *current) const {
        if (current != nullptr) {
            for (const OffThreadRuntime &entry : _offThreadRuntimes) {
                if (entry.runtime->GetIsolate() == current) {
                    return entry.runtime.get();
                }
            }
        }
        if (!OffEngineThread()) {
            return nullptr;
        }
        const OffThreadBinding &bound = tOffThreadBinding;
        if (bound.set == _offThreadSet && bound.index < _offThreadRuntimes.size()) {
            return _offThreadRuntimes[bound.index].runtime.get();
        }
        for (const OffThreadRuntime &entry : _offThreadRuntimes) {
            if (entry.name == kDefaultOffThreadRuntime) {
                return entry.runtime.get();
            }
        }
        return nullptr;
    }

    bool NodeEngine::CreateOffThreadRuntime(std::string_view name) {
        if (!_initialized) {
            return false;
        }
        for (const OffThreadRuntime &entry : _offThreadRuntimes) {
            if (entry.name == name) {
                return true;
            }
        }
        std::unique_ptr<NodeRuntime> runtime = CreateRuntime(_lastError);
        if (!runtime) {
            return false;
        }
        runtime->SetName(std::string(name));
        v8::Isolate *isolate = runtime->GetIsolate();
        // It never runs script on the engine's thread, so there is nothing to ask it a safe point for.
        UnregisterIsolate(isolate);

        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = runtime->GetContext();
        v8::Context::Scope contextScope(context);

        if (_offThreadRuntimes.empty()) {
            _offThreadSet = gNextOffThreadSet.fetch_add(1);
        }
        const uint32_t reference = _references->AddRuntime(isolate, context, std::string(name));
        _offThreadRuntimes.push_back({std::string(name), std::move(runtime), reference});
        // GetIsolate() names this runtime while it is entered, so the bindings land in it.
        if (_runtimeSetupCallback) {
            _runtimeSetupCallback(this);
        }
        return true;
    }

    void NodeEngine::BindOffThreadRuntime(std::string_view name) {
        for (size_t index = 0; index < _offThreadRuntimes.size(); ++index) {
            if (_offThreadRuntimes[index].name == name) {
                tOffThreadBinding = {_offThreadSet, index};
                return;
            }
        }
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("No off-thread scripting runtime is named '{}'; this thread stays on '{}'", name, kDefaultOffThreadRuntime);
    }

    v8::Isolate *NodeEngine::GetIsolate() const {
        if (!_runtime) {
            return nullptr;
        }
        v8::Isolate *current = v8::Isolate::TryGetCurrent();
        // A resource's runtime the caller is inside comes first, on any thread: one started from the tick thread
        // is set up there.
        if (current != nullptr && _runtimeByIsolate.contains(current)) {
            return current;
        }
        if (NodeRuntime *offThread = OffThreadRuntimeFor(current)) {
            return offThread->GetIsolate();
        }
        return _runtime->GetIsolate();
    }

    v8::Local<v8::Context> NodeEngine::GetContext() const {
        if (!_runtime) {
            return v8::Local<v8::Context>();
        }
        v8::Isolate *current = v8::Isolate::TryGetCurrent();
        if (current != nullptr) {
            const auto it = _runtimeByIsolate.find(current);
            if (it != _runtimeByIsolate.end()) {
                return it->second->runtime->GetContext();
            }
        }
        if (NodeRuntime *offThread = OffThreadRuntimeFor(current)) {
            return offThread->GetContext();
        }
        return _runtime->GetContext();
    }

    bool NodeEngine::CreateResourceRuntime(const std::string &resourceName) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }
        if (_resourceRuntimes.contains(resourceName)) {
            return true;
        }

        auto entry          = std::make_unique<ResourceRuntime>();
        entry->resourceName = resourceName;
        entry->engine       = this;
        entry->serial       = _nextRuntimeSerial++;
        entry->runtime      = CreateRuntime(_lastError);
        if (!entry->runtime) {
            return false;
        }
        entry->runtime->SetName(resourceName);
        entry->runtime->JoinSchedule(_schedule);

        ResourceRuntime *runtime = entry.get();
        v8::Isolate *isolate     = runtime->runtime->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = runtime->runtime->GetContext();
        v8::Context::Scope contextScope(context);

        // A sandboxed engine runs resources sandboxed: their runtimes are where their code runs.
        if (_options.sandboxed && !ApplySandbox(context)) {
            _lastError = "Failed to apply sandbox: " + _lastError;
            return false;
        }

        // process.exit() ends the resource, not the server. Node's default handler would exit the process; this stops
        // the resource's JavaScript at once and leaves stopping the resource to the next tick.
        node::SetProcessExitHandler(runtime->runtime->GetEnvironment(), [this, runtime](node::Environment *env, int code) {
            if (!runtime->exited) {
                runtime->exited = true;
                _pendingExits.push_back({runtime->resourceName, runtime->serial, code});
            }
            node::Stop(env);
        });

        _runtimeByIsolate[isolate]      = runtime;
        _resourceRuntimes[resourceName] = std::move(entry);
        ++_runtimesGeneration;
        _monitor.Track(isolate, resourceName);

        // Uncaught errors in this runtime can only be this resource's; no stack to read.
        v8::Local<v8::Function> sink = v8::FunctionTemplate::New(isolate, OnResourceUncaughtError, v8::External::New(isolate, runtime))->GetFunction(context).ToLocalChecked();
        context->Global()->DefineOwnProperty(context, v8::String::NewFromUtf8Literal(isolate, "__fw_handleUncaughtError"), sink, static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum | v8::DontDelete)).Check();

        runtime->reference = _references->AddRuntime(isolate, context, resourceName);

        // GetIsolate() names this runtime while it is entered, so the bindings land in it.
        if (_runtimeSetupCallback) {
            _runtimeSetupCallback(this);
        }
        return true;
    }

    void NodeEngine::DestroyResourceRuntime(const std::string &resourceName) {
        const auto it = _resourceRuntimes.find(resourceName);
        if (it == _resourceRuntimes.end()) {
            return;
        }
        std::unique_ptr<ResourceRuntime> runtime = std::move(it->second);
        _resourceRuntimes.erase(it);
        ++_runtimesGeneration;

        // Stopped while any script is running, its own or another's (an event handler stopping another resource,
        // say): native code further up the stack may still hold handles into it, such as an emission's snapshot of
        // its handlers. It is freed at the next tick, from the top. The name is free at once, so a restart can
        // create its next runtime.
        if (!CanTeardownNow(*runtime)) {
            runtime->retired = true;
            _retiredRuntimes.push_back(std::move(runtime));
            return;
        }
        TeardownResourceRuntime(std::move(runtime));
    }

    void NodeEngine::TeardownResourceRuntime(std::unique_ptr<ResourceRuntime> runtime) {
        v8::Isolate *isolate = runtime->runtime->GetIsolate();
        {
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);

            _references->RemoveRuntime(runtime->reference);

            // Handles the builtins keep in this isolate (an emission still waiting, a queued reply) go while it lives.
            if (_resourceManager != nullptr) {
                _resourceManager->OnRuntimeDisposing(isolate);
            }
            for (const auto &listener : _runtimeDisposingListeners) {
                listener(isolate);
            }
        }
        Builtins::UnregisterAll(isolate);
        ClearScriptingCatalog(isolate);
        _runtimeByIsolate.erase(isolate);
        _monitor.Untrack(isolate);

        // Frees the environment: its timers, handles and worker threads stop with it.
        runtime->runtime.reset();
    }

    bool NodeEngine::CanTeardownNow(const ResourceRuntime &runtime) const {
        return v8::Isolate::TryGetCurrent() == nullptr && !runtime.runtime->GetIsolate()->IsInUse();
    }

    void NodeEngine::FlushRetiredRuntimes() {
        // Taken out first: a teardown runs listeners that can stop, and so retire, further runtimes.
        std::vector<std::unique_ptr<ResourceRuntime>> retired;
        retired.swap(_retiredRuntimes);
        for (auto &runtime : retired) {
            if (CanTeardownNow(*runtime)) {
                TeardownResourceRuntime(std::move(runtime));
            }
            else {
                _retiredRuntimes.push_back(std::move(runtime));
            }
        }
    }

    bool NodeEngine::ExecuteResourceFile(const std::string &resourceName, std::string_view filepath) {
        const auto it = _resourceRuntimes.find(resourceName);
        if (it == _resourceRuntimes.end()) {
            _lastError = "Resource '" + resourceName + "' has no runtime";
            return false;
        }

        // Entered, the runtime is the one GetIsolate() names, so the file runs in it.
        v8::Isolate *isolate = it->second->runtime->GetIsolate();
        ResourceMonitor::Scope charged(_monitor, isolate);
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        return ExecuteFile(filepath);
    }

    std::string NodeEngine::GetResourceForIsolate(v8::Isolate *isolate) const {
        const auto it = _runtimeByIsolate.find(isolate);
        return it != _runtimeByIsolate.end() ? it->second->resourceName : std::string();
    }

    NodeRuntime *NodeEngine::GetResourceRuntime(const std::string &resourceName) const {
        const auto it = _resourceRuntimes.find(resourceName);
        return it != _resourceRuntimes.end() ? it->second->runtime.get() : nullptr;
    }

    void NodeEngine::InstallUncaughtExceptionHandler(const std::string &resourcesPath) {
        // Store canonical resources path for extracting resource names from error stacks
        std::error_code ec;
        auto canonicalPath = std::filesystem::weakly_canonical(resourcesPath, ec);
        _resourcesPath     = ec ? resourcesPath : canonicalPath.string();

        // Create C++ handler function accessible from JS
        v8::Local<v8::Context> context       = _runtime->GetContext();
        v8::Local<v8::External> data         = v8::External::New(GetIsolate(), this);
        v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(GetIsolate(), OnUncaughtError, data);
        v8::Local<v8::Function> fn           = tmpl->GetFunction(context).ToLocalChecked();

        v8::Local<v8::String> key = v8::String::NewFromUtf8(GetIsolate(), "__fw_handleUncaughtError").ToLocalChecked();
        context->Global()->Set(context, key, fn).Check();
    }

    std::string NodeEngine::ReadUncaughtError(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        std::string errorMsg = "Unknown error";
        std::string origin   = "uncaughtException";
        if (info.Length() > 0) {
            v8::String::Utf8Value msg(isolate, info[0]);
            if (*msg) {
                errorMsg = *msg;
            }
        }
        if (info.Length() > 1) {
            v8::String::Utf8Value orig(isolate, info[1]);
            if (*orig) {
                origin = *orig;
            }
        }
        return "[" + origin + "] " + errorMsg;
    }

    void NodeEngine::OnResourceUncaughtError(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *runtime = static_cast<ResourceRuntime *>(info.Data().As<v8::External>()->Value());

        // Queued for Tick()'s caller, like the engine's own errors.
        runtime->engine->_pendingErrors.push_back({runtime->resourceName, ReadUncaughtError(info), runtime->serial});
    }

    void NodeEngine::OnUncaughtError(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *engine            = static_cast<NodeEngine *>(v8::Local<v8::External>::Cast(info.Data())->Value());
        const std::string error = ReadUncaughtError(info);

        // Try to extract resource name from the error stack trace by matching
        // file paths against the configured resources directory
        std::string resourceName;
        if (!engine->_resourcesPath.empty()) {
            // Normalize path separators for cross-platform matching
            std::string normalizedError   = error;
            std::string normalizedResPath = engine->_resourcesPath;
            std::replace(normalizedError.begin(), normalizedError.end(), '\\', '/');
            std::replace(normalizedResPath.begin(), normalizedResPath.end(), '\\', '/');

            if (!normalizedResPath.empty() && normalizedResPath.back() != '/') {
                normalizedResPath += '/';
            }

            size_t pos = normalizedError.find(normalizedResPath);
            if (pos != std::string::npos) {
                size_t nameStart = pos + normalizedResPath.size();
                size_t nameEnd   = normalizedError.find('/', nameStart);
                if (nameEnd != std::string::npos) {
                    resourceName = normalizedError.substr(nameStart, nameEnd - nameStart);
                }
            }
        }

        // Queue for processing outside of Tick()
        engine->_pendingErrors.push_back({resourceName.empty() ? "unknown" : resourceName, error});
    }

    std::vector<NodeEngine::PendingUncaughtError> NodeEngine::DrainPendingErrors() {
        std::vector<PendingUncaughtError> errors;
        errors.swap(_pendingErrors);
        return errors;
    }

    bool NodeEngine::IsFromCurrentRuntime(const PendingUncaughtError &error) const {
        if (error.serial == 0) {
            return true;
        }
        const auto it = _resourceRuntimes.find(error.resourceName);
        return it != _resourceRuntimes.end() && it->second->serial == error.serial;
    }

    bool NodeEngine::ApplySandbox(v8::Local<v8::Context> context) {
        // This function disables dangerous Node.js APIs for client-side sandboxing.
        // We override require() to block dangerous modules and remove dangerous
        // properties from the global scope and process object.

        const char *sandboxCode = R"JS(
(function() {
    'use strict';

    // List of modules that are blocked in sandbox mode
    const blockedModules = new Set([
        // Filesystem access
        'fs', 'fs/promises', 'node:fs', 'node:fs/promises',

        // Network access
        'net', 'node:net',
        'dgram', 'node:dgram',
        'tls', 'node:tls',
        'http', 'node:http',
        'https', 'node:https',
        'http2', 'node:http2',
        'dns', 'node:dns',
        'dns/promises', 'node:dns/promises',

        // Process spawning
        'child_process', 'node:child_process',

        // Threading
        'worker_threads', 'node:worker_threads',
        'cluster', 'node:cluster',

        // Other dangerous modules
        'vm', 'node:vm',
        'v8', 'node:v8',
        'trace_events', 'node:trace_events',
        'perf_hooks', 'node:perf_hooks',
        'async_hooks', 'node:async_hooks',
        'diagnostics_channel', 'node:diagnostics_channel',
        'repl', 'node:repl',
        'readline', 'node:readline',
        'readline/promises', 'node:readline/promises',
        'module', 'node:module',
        'wasi', 'node:wasi',
        'sqlite', 'node:sqlite',
        'sea', 'node:sea',
    ]);

    // Store original require
    const originalRequire = globalThis.require;

    // Create sandboxed require that blocks dangerous modules
    function sandboxedRequire(id) {
        if (blockedModules.has(id)) {
            throw new Error(`Module '${id}' is not available in sandbox mode`);
        }

        // For non-builtin modules (npm packages, local files), allow if not in blocked list
        // But we need to be careful about packages that re-export blocked modules
        return originalRequire(id);
    }

    // Keep resolve() for compatibility, but never expose cache/main internals.
    sandboxedRequire.resolve = function(id, options) {
        if (blockedModules.has(id)) {
            throw new Error(`Module '${id}' is not available in sandbox mode`);
        }
        return originalRequire.resolve(id, options);
    };

    // Replace global require and prevent user code from swapping it back.
    Object.defineProperty(globalThis, 'require', {
        value: sandboxedRequire,
        writable: false,
        configurable: false,
        enumerable: true
    });

    // Disable dangerous process methods and properties
    const process = globalThis.process;

    // Remove access to environment variables (could leak sensitive info)
    process.env = Object.freeze({});

    // Disable process control methods
    process.exit = function() {
        throw new Error('process.exit() is not available in sandbox mode');
    };
    process.abort = function() {
        throw new Error('process.abort() is not available in sandbox mode');
    };
    process.kill = function() {
        throw new Error('process.kill() is not available in sandbox mode');
    };
    process.chdir = function() {
        throw new Error('process.chdir() is not available in sandbox mode');
    };
    process.umask = function() {
        throw new Error('process.umask() is not available in sandbox mode');
    };
    process.setuid = function() {
        throw new Error('process.setuid() is not available in sandbox mode');
    };
    process.setgid = function() {
        throw new Error('process.setgid() is not available in sandbox mode');
    };
    process.seteuid = function() {
        throw new Error('process.seteuid() is not available in sandbox mode');
    };
    process.setegid = function() {
        throw new Error('process.setegid() is not available in sandbox mode');
    };
    process.setgroups = function() {
        throw new Error('process.setgroups() is not available in sandbox mode');
    };
    process.initgroups = function() {
        throw new Error('process.initgroups() is not available in sandbox mode');
    };

    // Disable dlopen (loading native modules)
    process.dlopen = function() {
        throw new Error('process.dlopen() is not available in sandbox mode');
    };

    // Disable binding (internal Node.js APIs)
    process.binding = function() {
        throw new Error('process.binding() is not available in sandbox mode');
    };
    process._linkedBinding = function() {
        throw new Error('process._linkedBinding() is not available in sandbox mode');
    };

    // Remove reference to main module (prevents path discovery)
    process.mainModule = undefined;

    // Disable code generation from strings (eval, Function constructor)
    // This is also set at the C++ level but we reinforce it here
    // Note: This would require context-level settings which we do in C++

    // Block inspector module unless explicitly enabled for debugging
    if (!globalThis.__INSPECTOR_ENABLED__) {
        blockedModules.add('inspector');
        blockedModules.add('node:inspector');
        blockedModules.add('inspector/promises');
        blockedModules.add('node:inspector/promises');
    }

    // Mark sandbox as applied
    globalThis.__SANDBOX_APPLIED__ = true;
})();
)JS";

        v8::Isolate *isolate = v8::Isolate::GetCurrent();

        // Set inspector flag before sandbox code runs so it can conditionally
        // allow the inspector module for debugging
        if (_options.enableInspector) {
            v8::Local<v8::String> key = v8::String::NewFromUtf8(isolate, "__INSPECTOR_ENABLED__").ToLocalChecked();
            context->Global()->Set(context, key, v8::Boolean::New(isolate, true)).Check();
        }

        v8::TryCatch tryCatch(isolate);

        v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, sandboxCode).ToLocalChecked();
        v8::ScriptOrigin origin(v8::String::NewFromUtf8(isolate, "<sandbox-init>").ToLocalChecked());

        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
            if (tryCatch.HasCaught()) {
                _lastError = FormatV8Exception(isolate, tryCatch, "Sandbox script compilation error");
            }
            else {
                _lastError = "Failed to compile sandbox script";
            }
            return false;
        }

        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
            if (tryCatch.HasCaught()) {
                _lastError = FormatV8Exception(isolate, tryCatch, "Sandbox script execution error");
            }
            else {
                _lastError = "Failed to execute sandbox script";
            }
            return false;
        }

        // Also disable code generation from strings at the V8 level
        context->AllowCodeGenerationFromStrings(false);

        return true;
    }

} // namespace Framework::Scripting
