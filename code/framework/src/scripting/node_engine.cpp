/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "node_engine.h"
#include "engine_helpers.h"
#include "builtins/builtins.h"
#include "builtins/messages.h"
#include "resource/resource_manager.h"
#include "scripting_catalog.h"

#include <logging/logger.h>

#include <algorithm>
#include <filesystem>


namespace {
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
    constexpr const char *kRuntimeBootstrap =
        "const publicRequire = require('node:module').createRequire(process.cwd() + '/');"
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

    std::unique_ptr<node::MultiIsolatePlatform> NodeEngine::_platform = nullptr;
    std::shared_ptr<node::InitializationResult> NodeEngine::_initResult = nullptr;
    bool NodeEngine::_platformInitialized = false;

    NodeEngine::NodeEngine(const NodeEngineOptions &options)
        : _options(options) {}

    NodeEngine::~NodeEngine() {
        Shutdown();
    }

    ScriptingError NodeEngine::Init() {
        _lastError.clear();

        if (_initialized) {
            return ScriptingError::SCRIPTING_NONE;
        }

        if (!InitializeNode()) {
            return ScriptingError::SCRIPTING_PLATFORM_INIT_FAILED;
        }

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

        // Resources have normally stopped by now; any runtime left goes before the engine's own.
        FlushRetiredRuntimes();
        while (!_resourceRuntimes.empty()) {
            auto node = _resourceRuntimes.extract(_resourceRuntimes.begin());
            TeardownResourceRuntime(std::move(node.mapped()));
        }

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
        _platform = node::MultiIsolatePlatform::Create(4);
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
            v8::Local<v8::Context> ctx = _runtime->GetContext();
            v8::Local<v8::String> source = v8::String::NewFromUtf8Literal(
                GetIsolate(), "(function(){ setImmediate(function(){}); })");
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
            v8::Isolate *isolate = _runtime->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _runtime->GetContext();
            v8::Context::Scope contextScope(context);
            _interruptDrainFn.Get(isolate)->Call(context, v8::Undefined(isolate), 0, nullptr).FromMaybe(v8::Local<v8::Value>());
        }
#endif

        StopExitedResources();
        FlushRetiredRuntimes();
        _references->ReleaseCollected();

        // A runtime that is executing further up this thread's stack is not ticked from inside itself: uv_run does
        // not nest.
        if (!_runtime->GetIsolate()->IsInUse()) {
            _runtime->Tick();
        }

        TickResourceRuntimes();

        StopExitedResources();
        FlushRetiredRuntimes();
    }

    void NodeEngine::TickResourceRuntimes() {
        for (auto it = _resourceRuntimes.begin(); it != _resourceRuntimes.end();) {
            ResourceRuntime &entry = *it->second;
            const uint64_t generation = _runtimesGeneration;
            if (!entry.exited && !entry.runtime->GetIsolate()->IsInUse()) {
                entry.runtime->Tick();
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

    void NodeEngine::StopExitedResources() {
        while (!_pendingExits.empty()) {
            std::vector<PendingExit> exits;
            exits.swap(_pendingExits);
            for (const auto &exit : exits) {
                // A runtime the resource has since been restarted in did not exit.
                const auto it = _resourceRuntimes.find(exit.resourceName);
                if (it == _resourceRuntimes.end() || it->second->serial != exit.serial) {
                    continue;
                }
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Resource '{}' called process.exit({}); stopping the resource", exit.resourceName, exit.code);
                if (_resourceManager != nullptr && _resourceManager->IsResourceRunning(exit.resourceName)) {
                    _resourceManager->StopResource(exit.resourceName);
                }
                else {
                    DestroyResourceRuntime(exit.resourceName);
                }
            }
        }
    }

    bool NodeEngine::ExecuteFile(std::string_view filepath) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }

        // Convert to absolute path for Node.js require
        // Use generic_string() to get forward slashes on all platforms
        std::filesystem::path absPath = std::filesystem::absolute(filepath);
        std::string absPathStr = absPath.generic_string();

        // Use Node.js require for file execution
        // Escape the path for safe embedding in JS single-quoted string
        std::string escapedPath = EscapeForSingleQuotedJSString(absPathStr);
        std::string code = "require('" + escapedPath + "');";
        return Execute(code, absPathStr);
    }

    v8::Isolate *NodeEngine::GetIsolate() const {
        if (!_runtime) {
            return nullptr;
        }
        v8::Isolate *current = v8::Isolate::TryGetCurrent();
        if (current != nullptr && _runtimeByIsolate.contains(current)) {
            return current;
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

        // Stopped from inside itself (or from a call it is waiting on): freeing it now would pull the isolate out
        // from under the code that is running. The name is free at once, so a restart can create its next runtime.
        if (runtime->runtime->GetIsolate()->IsInUse()) {
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

            // process.on('exit') handlers run while the bindings are still there, as Node runs them before it frees an
            // environment. One that called process.exit() has emitted 'exit' already.
            if (!runtime->exited) {
                v8::Context::Scope contextScope(runtime->runtime->GetContext());
                (void)node::EmitProcessExit(runtime->runtime->GetEnvironment()).FromMaybe(0);
            }

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

        // Frees the environment: its timers, handles and worker threads stop with it.
        runtime->runtime.reset();
    }

    void NodeEngine::FlushRetiredRuntimes() {
        for (auto it = _retiredRuntimes.begin(); it != _retiredRuntimes.end();) {
            if ((*it)->runtime->GetIsolate()->IsInUse()) {
                ++it;
                continue;
            }
            std::unique_ptr<ResourceRuntime> runtime = std::move(*it);
            it                                       = _retiredRuntimes.erase(it);
            TeardownResourceRuntime(std::move(runtime));
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
        _resourcesPath = ec ? resourcesPath : canonicalPath.string();

        // Create C++ handler function accessible from JS
        v8::Local<v8::Context> context = _runtime->GetContext();
        v8::Local<v8::External> data = v8::External::New(GetIsolate(), this);
        v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(
            GetIsolate(), OnUncaughtError, data);
        v8::Local<v8::Function> fn = tmpl->GetFunction(context).ToLocalChecked();

        v8::Local<v8::String> key = v8::String::NewFromUtf8(
            GetIsolate(), "__fw_handleUncaughtError").ToLocalChecked();
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
        runtime->engine->_pendingErrors.push_back({runtime->resourceName, ReadUncaughtError(info)});
    }

    void NodeEngine::OnUncaughtError(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *engine = static_cast<NodeEngine *>(
            v8::Local<v8::External>::Cast(info.Data())->Value());
        const std::string error = ReadUncaughtError(info);

        // Try to extract resource name from the error stack trace by matching
        // file paths against the configured resources directory
        std::string resourceName;
        if (!engine->_resourcesPath.empty()) {
            // Normalize path separators for cross-platform matching
            std::string normalizedError = error;
            std::string normalizedResPath = engine->_resourcesPath;
            std::replace(normalizedError.begin(), normalizedError.end(), '\\', '/');
            std::replace(normalizedResPath.begin(), normalizedResPath.end(), '\\', '/');

            if (!normalizedResPath.empty() && normalizedResPath.back() != '/') {
                normalizedResPath += '/';
            }

            size_t pos = normalizedError.find(normalizedResPath);
            if (pos != std::string::npos) {
                size_t nameStart = pos + normalizedResPath.size();
                size_t nameEnd = normalizedError.find('/', nameStart);
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

        v8::Isolate *isolate = context->GetIsolate();

        // Set inspector flag before sandbox code runs so it can conditionally
        // allow the inspector module for debugging
        if (_options.enableInspector) {
            v8::Local<v8::String> key =
                v8::String::NewFromUtf8(isolate, "__INSPECTOR_ENABLED__").ToLocalChecked();
            context->Global()->Set(context, key, v8::Boolean::New(isolate, true)).Check();
        }

        v8::TryCatch tryCatch(isolate);

        v8::Local<v8::String> source =
            v8::String::NewFromUtf8(isolate, sandboxCode).ToLocalChecked();
        v8::ScriptOrigin origin(
            v8::String::NewFromUtf8(isolate, "<sandbox-init>").ToLocalChecked());

        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
            if (tryCatch.HasCaught()) {
                _lastError = FormatV8Exception(isolate, tryCatch, "Sandbox script compilation error");
            } else {
                _lastError = "Failed to compile sandbox script";
            }
            return false;
        }

        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
            if (tryCatch.HasCaught()) {
                _lastError = FormatV8Exception(isolate, tryCatch, "Sandbox script execution error");
            } else {
                _lastError = "Failed to execute sandbox script";
            }
            return false;
        }

        // Also disable code generation from strings at the V8 level
        context->AllowCodeGenerationFromStrings(false);

        return true;
    }

} // namespace Framework::Scripting
