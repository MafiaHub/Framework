/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "node_engine.h"
#include "engine_helpers.h"
#include "builtins/messages.h"

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
        if (!_initialized || !_setup) {
            _initialized = false;
            return;
        }

        // Following Node.js embedtest.cc pattern exactly:
        // 1. V8 scopes in a block for any final JS operations
        // 2. Scopes exit when block ends
        // 3. node::Stop() called AFTER scopes are released
        // 4. CommonEnvironmentSetup destructor runs last
        {
            v8::Locker locker(_isolate);
            v8::Isolate::Scope isolate_scope(_isolate);
            v8::HandleScope handle_scope(_isolate);
            v8::Context::Scope context_scope(_setup->context());

            Messages::Shutdown();
        }
        // All V8 scopes have now exited

        // Stop Node.js environment AFTER scopes are released (per embedtest.cc)
        node::Stop(_env);

        // Release persistent handles before destroying the isolate
        _interruptDrainFn.Reset();

        // Clear our references before destroying setup
        _env = nullptr;
        _isolate = nullptr;

        // CommonEnvironmentSetup destructor handles isolate disposal
        _setup.reset();

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

        // Initialize Node.js with flags to control V8 platform ourselves
        // Using initializer_list syntax as shown in Node.js docs
        _initResult = node::InitializeOncePerProcess(
            nodeArgs,
            {
                node::ProcessInitializationFlags::kNoInitializeV8,
                node::ProcessInitializationFlags::kNoInitializeNodeV8Platform
            }
        );

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

    bool NodeEngine::CreateEnvironment() {
        // Use CommonEnvironmentSetup for proper Node.js embedding
        std::vector<std::string> errors;
        _setup = node::CommonEnvironmentSetup::Create(
            _platform.get(),
            &errors,
            _initResult->args(),
            _initResult->exec_args()
        );

        if (!_setup) {
            _lastError = "Failed to create Node.js environment setup";
            for (const auto &err : errors) {
                _lastError += "\n" + err;
            }
            return false;
        }

        _isolate = _setup->isolate();
        _env = _setup->env();

        // Enter scopes for LoadEnvironment as required by Node.js docs
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolate_scope(_isolate);
        v8::HandleScope handle_scope(_isolate);
        v8::Context::Scope context_scope(_setup->context());

        // Load Node.js internals with require setup and uncaught exception/rejection
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
        v8::MaybeLocal<v8::Value> loadResult = node::LoadEnvironment(
            _env,
            "const publicRequire = require('node:module').createRequire(process.cwd() + '/');"
            "globalThis.require = publicRequire;"
            "globalThis.Framework = {};"
            "globalThis.Core = {};"
            // Privileged hot-reload hook: capture the real CommonJS module cache
            // now, before require() is sandboxed (which hides require.cache).
            // Lets C++ evict a resource's modules on reload.
            "Object.defineProperty(globalThis, '__fw_evictModulesUnderPath', {"
            "  value: function(root, ci) {"
            "    try {"
            "      const cache = publicRequire.cache; if (!cache) return 0;"
            "      let r = String(root).replace(/\\\\/g, '/'); if (ci) r = r.toLowerCase();"
            "      let removed = 0;"
            "      for (const k of Object.keys(cache)) {"
            "        let nk = k.replace(/\\\\/g, '/'); if (ci) nk = nk.toLowerCase();"
            "        if (nk.indexOf(r) === 0) { delete cache[k]; removed++; }"
            "      }"
            "      return removed;"
            "    } catch (e) { return 0; }"
            "  }, writable: false, configurable: false, enumerable: false"
            "});"
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
            "});"
        );

        if (loadResult.IsEmpty()) {
            _lastError = "Failed to load Node.js environment";
            return false;
        }

        // Apply sandbox restrictions if enabled
        if (_options.sandboxed && !ApplySandbox()) {
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
            v8::Local<v8::Context> ctx = _setup->context();
            v8::Local<v8::String> source = v8::String::NewFromUtf8Literal(
                _isolate, "(function(){ setImmediate(function(){}); })");
            v8::Local<v8::Script> script;
            if (v8::Script::Compile(ctx, source).ToLocal(&script)) {
                v8::Local<v8::Value> result;
                if (script->Run(ctx).ToLocal(&result) && result->IsFunction()) {
                    _interruptDrainFn.Reset(_isolate, result.As<v8::Function>());
                }
            }
        }
#endif

        return true;
    }

    void NodeEngine::Tick() {
        if (!_initialized || !_setup) {
            return;
        }

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolate_scope(_isolate);
        v8::HandleScope handle_scope(_isolate);
        v8::Local<v8::Context> context = _setup->context();
        v8::Context::Scope context_scope(context);

#ifdef FW_NODE_INSPECTOR
        // Trigger V8 interrupt processing for inspector CDP messages.
        // Calling setImmediate() enters JS (draining V8 interrupts at the
        // safepoint) and activates Node's CheckImmediate uv_check handle,
        // which calls RunAndClearNativeImmediates → RunAndClearInterrupts.
        if (!_interruptDrainFn.IsEmpty()) {
            _interruptDrainFn.Get(_isolate)->Call(context, v8::Undefined(_isolate), 0, nullptr)
                .FromMaybe(v8::Local<v8::Value>());
        }
#endif

        // Process microtasks (Promise continuations, async/await)
        _isolate->PerformMicrotaskCheckpoint();

        // Run pending libuv events (non-blocking)
        uv_run(_setup->event_loop(), UV_RUN_NOWAIT);

        // Drain V8 platform tasks (background compile, etc.)
        _platform->DrainTasks(_isolate);

        // Process any microtasks that were queued by I/O callbacks or platform tasks
        _isolate->PerformMicrotaskCheckpoint();
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

    void NodeEngine::EvictModulesUnderPath(const std::string &rootPath) {
        if (!_initialized) {
            return;
        }

        std::error_code ec;
        std::filesystem::path absRoot = std::filesystem::weakly_canonical(rootPath, ec);
        std::string rootStr = (ec ? std::filesystem::path(rootPath) : absRoot).generic_string();
        if (rootStr.empty()) {
            return;
        }
        // Trailing slash enforces a directory boundary in the JS startsWith
        // check so /res/a doesn't evict modules of /res/ab.
        if (rootStr.back() != '/') {
            rootStr += '/';
        }

#ifdef _WIN32
        const char *caseInsensitive = "true";
#else
        const char *caseInsensitive = "false";
#endif
        std::string escaped = EscapeForSingleQuotedJSString(rootStr);
        std::string code =
            "if (typeof globalThis.__fw_evictModulesUnderPath === 'function')"
            " globalThis.__fw_evictModulesUnderPath('" + escaped + "', " + caseInsensitive + ");";
        Execute(code, "<evict-modules>");
    }

    v8::Local<v8::Context> NodeEngine::GetContext() const {
        if (_setup) {
            return _setup->context();
        }
        return v8::Local<v8::Context>();
    }

    void NodeEngine::InstallUncaughtExceptionHandler(const std::string &resourcesPath) {
        // Store canonical resources path for extracting resource names from error stacks
        std::error_code ec;
        auto canonicalPath = std::filesystem::weakly_canonical(resourcesPath, ec);
        _resourcesPath = ec ? resourcesPath : canonicalPath.string();

        // Create C++ handler function accessible from JS
        v8::Local<v8::Context> context = _setup->context();
        v8::Local<v8::External> data = v8::External::New(_isolate, this);
        v8::Local<v8::FunctionTemplate> tmpl = v8::FunctionTemplate::New(
            _isolate, OnUncaughtError, data);
        v8::Local<v8::Function> fn = tmpl->GetFunction(context).ToLocalChecked();

        v8::Local<v8::String> key = v8::String::NewFromUtf8(
            _isolate, "__fw_handleUncaughtError").ToLocalChecked();
        context->Global()->Set(context, key, fn).Check();
    }

    void NodeEngine::OnUncaughtError(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        auto *engine = static_cast<NodeEngine *>(
            v8::Local<v8::External>::Cast(info.Data())->Value());

        std::string errorMsg = "Unknown error";
        std::string origin = "uncaughtException";

        if (info.Length() > 0) {
            v8::String::Utf8Value msg(isolate, info[0]);
            if (*msg) errorMsg = *msg;
        }
        if (info.Length() > 1) {
            v8::String::Utf8Value orig(isolate, info[1]);
            if (*orig) origin = *orig;
        }

        // Try to extract resource name from the error stack trace by matching
        // file paths against the configured resources directory
        std::string resourceName;
        if (!engine->_resourcesPath.empty()) {
            // Normalize path separators for cross-platform matching
            std::string normalizedError = errorMsg;
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
        engine->_pendingErrors.push_back({
            resourceName.empty() ? "unknown" : resourceName,
            "[" + origin + "] " + errorMsg
        });
    }

    std::vector<NodeEngine::PendingUncaughtError> NodeEngine::DrainPendingErrors() {
        std::vector<PendingUncaughtError> errors;
        errors.swap(_pendingErrors);
        return errors;
    }

    bool NodeEngine::ApplySandbox() {
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

        v8::Local<v8::Context> context = _setup->context();

        // Set inspector flag before sandbox code runs so it can conditionally
        // allow the inspector module for debugging
        if (_options.enableInspector) {
            v8::Local<v8::String> key =
                v8::String::NewFromUtf8(_isolate, "__INSPECTOR_ENABLED__").ToLocalChecked();
            context->Global()->Set(context, key, v8::Boolean::New(_isolate, true)).Check();
        }

        v8::TryCatch tryCatch(_isolate);

        v8::Local<v8::String> source =
            v8::String::NewFromUtf8(_isolate, sandboxCode).ToLocalChecked();
        v8::ScriptOrigin origin(
            v8::String::NewFromUtf8(_isolate, "<sandbox-init>").ToLocalChecked());

        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
            if (tryCatch.HasCaught()) {
                _lastError = FormatV8Exception(_isolate, tryCatch, "Sandbox script compilation error");
            } else {
                _lastError = "Failed to compile sandbox script";
            }
            return false;
        }

        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
            if (tryCatch.HasCaught()) {
                _lastError = FormatV8Exception(_isolate, tryCatch, "Sandbox script execution error");
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
