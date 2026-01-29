#include "node_engine.h"

#include <fstream>
#include <sstream>

namespace Framework::Scripting::JS {

    std::unique_ptr<node::MultiIsolatePlatform> NodeEngine::_platform = nullptr;
    bool NodeEngine::_platformInitialized = false;
    std::vector<std::string> NodeEngine::_nodeArgs = {"mafiahub-server"};

    NodeEngine::NodeEngine()
        : _isolateData(nullptr, node::FreeIsolateData) {
    }

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
        if (!_initialized) {
            return;
        }

        if (_env) {
            node::Stop(_env);
            node::FreeEnvironment(_env);
            _env = nullptr;
        }

        _isolateData.reset();

        if (_isolate) {
            _isolate->Dispose();
            _isolate = nullptr;
        }

        if (_uvLoopInitialized) {
            uv_loop_close(&_uvLoop);
            _uvLoopInitialized = false;
        }

        _initialized = false;
    }

    bool NodeEngine::InitializeNode() {
        if (_platformInitialized) {
            return true;
        }

        // Initialize Node.js
        std::vector<std::string> args = _nodeArgs;
        std::vector<std::string> execArgs;
        std::vector<std::string> errors;

        int exitCode = node::InitializeNodeWithArgs(&args, &execArgs, &errors);

        if (exitCode != 0) {
            _lastError = "Failed to initialize Node.js";
            for (const auto &err : errors) {
                _lastError += "\n" + err;
            }
            return false;
        }

        _platform = node::MultiIsolatePlatform::Create(4);
        v8::V8::InitializePlatform(_platform.get());
        v8::V8::Initialize();

        _platformInitialized = true;
        return true;
    }

    bool NodeEngine::CreateEnvironment() {
        // Initialize libuv loop
        int uvResult = uv_loop_init(&_uvLoop);
        if (uvResult != 0) {
            _lastError = "Failed to initialize libuv loop";
            return false;
        }
        _uvLoopInitialized = true;

        // Create allocator (use shared_ptr for Node.js API)
        _allocator = node::ArrayBufferAllocator::Create();

        // Create isolate
        _isolate = node::NewIsolate(_allocator, &_uvLoop, _platform.get());
        if (!_isolate) {
            _lastError = "Failed to create Node.js isolate";
            return false;
        }

        // Create isolate data
        _isolateData.reset(node::CreateIsolateData(_isolate, &_uvLoop, _platform.get(), _allocator.get()));

        // Enter isolate scope
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);

        // Create context
        v8::Local<v8::Context> context = node::NewContext(_isolate);
        if (context.IsEmpty()) {
            _lastError = "Failed to create Node.js context";
            return false;
        }

        // Store context globally for later access
        _context.Reset(_isolate, context);

        v8::Context::Scope contextScope(context);

        // Create Node.js environment
        _env = node::CreateEnvironment(_isolateData.get(), context, _nodeArgs, _nodeArgs);

        if (!_env) {
            _lastError = "Failed to create Node.js environment";
            return false;
        }

        // Load Node.js internals
        node::LoadEnvironment(_env, "const publicRequire = require('module').createRequire(process.cwd() + '/');"
                                    "globalThis.require = publicRequire;"
                                    "globalThis.Framework = {};");

        return true;
    }

    void NodeEngine::Tick() {
        if (!_initialized || !_env) {
            return;
        }

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);
        v8::Context::Scope contextScope(GetContext());

        // Run pending libuv events (non-blocking)
        uv_run(&_uvLoop, UV_RUN_NOWAIT);
    }

    bool NodeEngine::Execute(const std::string &code, const std::string &filename) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }

        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);
        v8::Local<v8::Context> context = GetContext();
        v8::Context::Scope contextScope(context);

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
        // Register N-API bindings for Framework APIs
        // This will be implemented in subsequent tasks

        if (_sdkRegisterCallback) {
            _sdkRegisterCallback(this);
        }

        return true;
    }

    v8::Local<v8::Context> NodeEngine::GetContext() const {
        if (!_context.IsEmpty()) {
            return _context.Get(_isolate);
        }
        return v8::Local<v8::Context>();
    }

} // namespace Framework::Scripting::JS
