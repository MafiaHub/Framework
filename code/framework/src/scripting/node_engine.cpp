#include "node_engine.h"

#include <fstream>
#include <sstream>

namespace Framework::Scripting {

    std::unique_ptr<node::MultiIsolatePlatform> NodeEngine::_platform = nullptr;
    std::shared_ptr<node::InitializationResult> NodeEngine::_initResult = nullptr;
    bool NodeEngine::_platformInitialized = false;
    std::vector<std::string> NodeEngine::_nodeArgs = {"mafiahub-server"};

    NodeEngine::NodeEngine() = default;

    NodeEngine::~NodeEngine() {
        Shutdown();
    }

    bool NodeEngine::Init() {
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

            // Any final JS cleanup can happen here if needed
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

        // Initialize Node.js with flags to control V8 platform ourselves
        // Using initializer_list syntax as shown in Node.js docs
        _initResult = node::InitializeOncePerProcess(
            _nodeArgs,
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
        );

        if (loadResult.IsEmpty()) {
            _lastError = "Failed to load Node.js environment";
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

        // Run pending libuv events (non-blocking)
        uv_run(_setup->event_loop(), UV_RUN_NOWAIT);
    }

    bool NodeEngine::Execute(const std::string &code, const std::string &filename) {
        if (!_initialized || !_setup) {
            _lastError = "Engine not initialized";
            return false;
        }

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolate_scope(_isolate);
        v8::HandleScope handle_scope(_isolate);
        v8::Local<v8::Context> context = _setup->context();
        v8::Context::Scope context_scope(context);

        v8::TryCatch tryCatch(_isolate);

        v8::Local<v8::String> source = v8::String::NewFromUtf8(_isolate, code.c_str()).ToLocalChecked();
        v8::ScriptOrigin origin(v8::String::NewFromUtf8(_isolate, filename.c_str()).ToLocalChecked());

        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
            v8::String::Utf8Value error(_isolate, tryCatch.Exception());
            _lastError = *error ? *error : "Compilation error";
            return false;
        }

        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
            v8::String::Utf8Value error(_isolate, tryCatch.Exception());
            _lastError = *error ? *error : "Runtime error";
            return false;
        }

        return true;
    }

    bool NodeEngine::ExecuteFile(const std::string &filepath) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }

        // Use Node.js require for file execution
        std::string code = "require('" + filepath + "');";
        return Execute(code, filepath);
    }

    bool NodeEngine::InitFrameworkSDK() {
        if (_sdkRegisterCallback) {
            _sdkRegisterCallback(this);
        }
        return true;
    }

    v8::Local<v8::Context> NodeEngine::GetContext() const {
        if (_setup) {
            return _setup->context();
        }
        return v8::Local<v8::Context>();
    }

} // namespace Framework::Scripting
