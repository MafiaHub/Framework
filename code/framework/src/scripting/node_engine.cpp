#include "node_engine.h"

#include <filesystem>
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

        // Process microtasks (Promise continuations, async/await)
        _isolate->PerformMicrotaskCheckpoint();

        // Run pending libuv events (non-blocking)
        uv_run(_setup->event_loop(), UV_RUN_NOWAIT);

        // Process any microtasks that were queued by I/O callbacks
        _isolate->PerformMicrotaskCheckpoint();
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

        // Use Node.js's vm.Script with USE_MAIN_CONTEXT_DEFAULT_LOADER for proper ES module support
        // This enables dynamic import() to use Node's module loader
        v8::Local<v8::Object> global = context->Global();

        // Get require function
        v8::Local<v8::Value> requireVal;
        v8::Local<v8::String> requireKey = v8::String::NewFromUtf8(_isolate, "require").ToLocalChecked();
        if (!global->Get(context, requireKey).ToLocal(&requireVal) ||
            !requireVal->IsFunction()) {
            _lastError = "require not available";
            return false;
        }
        v8::Local<v8::Function> requireFn = requireVal.As<v8::Function>();

        // require('vm')
        v8::Local<v8::Value> vmModuleArg = v8::String::NewFromUtf8(_isolate, "vm").ToLocalChecked();
        v8::Local<v8::Value> vmModule;
        if (!requireFn->Call(context, global, 1, &vmModuleArg).ToLocal(&vmModule) ||
            !vmModule->IsObject()) {
            _lastError = "Failed to require vm module";
            return false;
        }
        v8::Local<v8::Object> vmObj = vmModule.As<v8::Object>();

        // Get vm.Script constructor
        v8::Local<v8::Value> scriptClassVal;
        v8::Local<v8::String> scriptKey = v8::String::NewFromUtf8(_isolate, "Script").ToLocalChecked();
        if (!vmObj->Get(context, scriptKey).ToLocal(&scriptClassVal) || !scriptClassVal->IsFunction()) {
            _lastError = "vm.Script not available";
            return false;
        }
        v8::Local<v8::Function> scriptClass = scriptClassVal.As<v8::Function>();

        // Get vm.constants.USE_MAIN_CONTEXT_DEFAULT_LOADER
        v8::Local<v8::Value> constantsVal;
        v8::Local<v8::String> constantsKey = v8::String::NewFromUtf8(_isolate, "constants").ToLocalChecked();
        if (!vmObj->Get(context, constantsKey).ToLocal(&constantsVal) || !constantsVal->IsObject()) {
            _lastError = "vm.constants not available";
            return false;
        }
        v8::Local<v8::Value> loaderSymbol;
        v8::Local<v8::String> loaderKey = v8::String::NewFromUtf8(_isolate, "USE_MAIN_CONTEXT_DEFAULT_LOADER").ToLocalChecked();
        if (!constantsVal.As<v8::Object>()->Get(context, loaderKey).ToLocal(&loaderSymbol)) {
            _lastError = "vm.constants.USE_MAIN_CONTEXT_DEFAULT_LOADER not available";
            return false;
        }

        // Create options object: { filename, importModuleDynamically: USE_MAIN_CONTEXT_DEFAULT_LOADER }
        v8::Local<v8::Object> options = v8::Object::New(_isolate);
        v8::Local<v8::String> filenameKey = v8::String::NewFromUtf8(_isolate, "filename").ToLocalChecked();
        v8::Local<v8::String> filenameVal = v8::String::NewFromUtf8(_isolate, filename.c_str()).ToLocalChecked();
        options->Set(context, filenameKey, filenameVal).Check();
        v8::Local<v8::String> importKey = v8::String::NewFromUtf8(_isolate, "importModuleDynamically").ToLocalChecked();
        options->Set(context, importKey, loaderSymbol).Check();

        // Create new vm.Script(code, options)
        v8::Local<v8::String> codeStr = v8::String::NewFromUtf8(_isolate, code.c_str()).ToLocalChecked();
        v8::Local<v8::Value> ctorArgs[2] = {codeStr, options};
        v8::Local<v8::Object> scriptInstance;
        if (!scriptClass->NewInstance(context, 2, ctorArgs).ToLocal(&scriptInstance)) {
            if (tryCatch.HasCaught()) {
                v8::String::Utf8Value error(_isolate, tryCatch.Exception());
                _lastError = *error ? *error : "Script compilation error";
            } else {
                _lastError = "Failed to create vm.Script instance";
            }
            return false;
        }

        // Get script.runInThisContext method
        v8::Local<v8::Value> runMethodVal;
        v8::Local<v8::String> runKey = v8::String::NewFromUtf8(_isolate, "runInThisContext").ToLocalChecked();
        if (!scriptInstance->Get(context, runKey).ToLocal(&runMethodVal) || !runMethodVal->IsFunction()) {
            _lastError = "script.runInThisContext not available";
            return false;
        }
        v8::Local<v8::Function> runMethod = runMethodVal.As<v8::Function>();

        // Call script.runInThisContext()
        v8::Local<v8::Value> result;
        if (!runMethod->Call(context, scriptInstance, 0, nullptr).ToLocal(&result)) {
            if (tryCatch.HasCaught()) {
                v8::String::Utf8Value error(_isolate, tryCatch.Exception());
                _lastError = *error ? *error : "Runtime error";
            } else {
                _lastError = "Unknown execution error";
            }
            return false;
        }

        return true;
    }

    bool NodeEngine::ExecuteFile(const std::string &filepath) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }

        // Convert to absolute path for Node.js require
        // Use generic_string() to get forward slashes on all platforms,
        // avoiding Windows backslash escape sequence issues in JS strings
        std::filesystem::path absPath = std::filesystem::absolute(filepath);
        std::string absPathStr = absPath.generic_string();

        // Use Node.js require for file execution
        std::string code = "require('" + absPathStr + "');";
        return Execute(code, absPathStr);
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
