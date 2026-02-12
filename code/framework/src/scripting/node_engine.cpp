#include "node_engine.h"
#include "engine_helpers.h"
#include "builtins/messages.h"

#include <logging/logger.h>

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

    bool NodeEngine::Init() {
        _lastError.clear();

        if (_initialized) {
            return true;
        }

        if (!InitializeNode()) {
            return false;
        }

        if (!CreateEnvironment()) {
            return false;
        }

        _initialized = true;
        return true;
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

        // Load Node.js internals with require setup
        v8::MaybeLocal<v8::Value> loadResult = node::LoadEnvironment(
            _env,
            "const publicRequire = require('node:module').createRequire(process.cwd() + '/');"
            "globalThis.require = publicRequire;"
            "globalThis.Framework = {};"
            "globalThis.Core = {};"
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

        return true;
    }

    void NodeEngine::Tick() {
        if (!_initialized || !_setup) {
            return;
        }

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolate_scope(_isolate);
        v8::HandleScope handle_scope(_isolate);
        v8::Context::Scope context_scope(_setup->context());

        // Process microtasks (Promise continuations, async/await)
        _isolate->PerformMicrotaskCheckpoint();

        // Run pending libuv events (non-blocking)
        uv_run(_setup->event_loop(), UV_RUN_NOWAIT);

        // Process any microtasks that were queued by I/O callbacks
        _isolate->PerformMicrotaskCheckpoint();
    }

    bool NodeEngine::ExecuteFile(const std::string &filepath) {
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

    v8::Local<v8::Context> NodeEngine::GetContext() const {
        if (_setup) {
            return _setup->context();
        }
        return v8::Local<v8::Context>();
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

    // Modules that are allowed (safe subset)
    const allowedModules = new Set([
        // Core utilities
        'assert', 'node:assert',
        'assert/strict', 'node:assert/strict',
        'buffer', 'node:buffer',
        'console', 'node:console',
        'constants', 'node:constants',
        'crypto', 'node:crypto',
        'events', 'node:events',
        'path', 'node:path',
        'path/posix', 'node:path/posix',
        'path/win32', 'node:path/win32',
        'process', 'node:process',
        'punycode', 'node:punycode',
        'querystring', 'node:querystring',
        'stream', 'node:stream',
        'stream/consumers', 'node:stream/consumers',
        'stream/promises', 'node:stream/promises',
        'stream/web', 'node:stream/web',
        'string_decoder', 'node:string_decoder',
        'timers', 'node:timers',
        'timers/promises', 'node:timers/promises',
        'url', 'node:url',
        'util', 'node:util',
        'util/types', 'node:util/types',
        'zlib', 'node:zlib',
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

    // Copy properties from original require
    sandboxedRequire.resolve = function(id, options) {
        if (blockedModules.has(id)) {
            throw new Error(`Module '${id}' is not available in sandbox mode`);
        }
        return originalRequire.resolve(id, options);
    };
    sandboxedRequire.cache = originalRequire.cache;
    sandboxedRequire.main = originalRequire.main;

    // Replace global require
    globalThis.require = sandboxedRequire;

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
