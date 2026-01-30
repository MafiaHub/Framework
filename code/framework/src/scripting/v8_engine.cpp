#include "v8_engine.h"
#include "builtins/messages.h"

#include <fstream>
#include <limits>
#include <sstream>

namespace Framework::Scripting {

    std::unique_ptr<v8::Platform> V8Engine::_platform = nullptr;
    bool V8Engine::_platformInitialized = false;

    V8Engine::V8Engine() = default;

    V8Engine::~V8Engine() {
        Shutdown();
    }

    bool V8Engine::Init() {
        if (_initialized) {
            return true;
        }

        if (!InitializePlatform()) {
            return false;
        }

        if (!CreateIsolate()) {
            return false;
        }

        if (!CreateContext()) {
            _isolate->Dispose();
            _isolate = nullptr;
            delete _createParams.array_buffer_allocator;
            _createParams.array_buffer_allocator = nullptr;
            return false;
        }

        SetupSandbox();

        _initialized = true;
        return true;
    }

    void V8Engine::Shutdown() {
        if (!_initialized) {
            return;
        }

        Messages::Shutdown();
        _context.Reset();

        if (_isolate) {
            _isolate->Dispose();
            _isolate = nullptr;
        }

        auto *allocator = _createParams.array_buffer_allocator;
        _createParams.array_buffer_allocator = nullptr;
        delete allocator;

        _initialized = false;
    }

    bool V8Engine::InitializePlatform() {
        if (_platformInitialized) {
            return true;
        }

        v8::V8::InitializeICUDefaultLocation(nullptr);
        v8::V8::InitializeExternalStartupData(nullptr);

        _platform = v8::platform::NewDefaultPlatform();
        v8::V8::InitializePlatform(_platform.get());
        v8::V8::Initialize();

        _platformInitialized = true;
        return true;
    }

    bool V8Engine::CreateIsolate() {
        _createParams.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        _isolate = v8::Isolate::New(_createParams);

        if (!_isolate) {
            _lastError = "Failed to create V8 isolate";
            delete _createParams.array_buffer_allocator;
            _createParams.array_buffer_allocator = nullptr;
            return false;
        }

        return true;
    }

    bool V8Engine::CreateContext() {
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);

        // Create global template
        v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(_isolate);

        // Create context with global template
        v8::Local<v8::Context> context = v8::Context::New(_isolate, nullptr, global);

        if (context.IsEmpty()) {
            _lastError = "Failed to create V8 context";
            return false;
        }

        _context.Reset(_isolate, context);

        // Create Framework global object
        v8::Context::Scope contextScope(context);
        v8::Local<v8::Object> frameworkObj = v8::Object::New(_isolate);
        context->Global()
            ->Set(context, v8::String::NewFromUtf8(_isolate, "Framework").ToLocalChecked(), frameworkObj)
            .Check();

        return true;
    }

    void V8Engine::SetupSandbox() {
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);
        v8::Local<v8::Context> context = _context.Get(_isolate);
        v8::Context::Scope contextScope(context);

        // Disable eval() and Function() constructor for client sandbox
        context->AllowCodeGenerationFromStrings(false);
    }

    bool V8Engine::Execute(const std::string &code, const std::string &filename) {
        if (!_initialized) {
            _lastError = "Engine not initialized";
            return false;
        }

        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);
        v8::Local<v8::Context> context = _context.Get(_isolate);
        v8::Context::Scope contextScope(context);

        v8::TryCatch tryCatch(_isolate);

        if (code.length() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            _lastError = "Code string too large";
            return false;
        }

        v8::Local<v8::String> source =
            v8::String::NewFromUtf8(_isolate, code.c_str(), v8::NewStringType::kNormal, static_cast<int>(code.length()))
                .ToLocalChecked();

        v8::ScriptOrigin origin(v8::String::NewFromUtf8(_isolate, filename.c_str()).ToLocalChecked());

        v8::Local<v8::Script> script;
        if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
            _lastError = FormatException(tryCatch);
            return false;
        }

        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
            _lastError = FormatException(tryCatch);
            return false;
        }

        return true;
    }

    bool V8Engine::ExecuteFile(const std::string &filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            _lastError = "Failed to open file: " + filepath;
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return Execute(buffer.str(), filepath);
    }

    bool V8Engine::InitFrameworkSDK() {
        // Register math types and other builtins
        // This will be implemented in subsequent tasks

        if (_sdkRegisterCallback) {
            _sdkRegisterCallback(this);
        }

        return true;
    }

    v8::Local<v8::Object> V8Engine::GetFrameworkObject() const {
        v8::Local<v8::Context> context = _context.Get(_isolate);
        v8::Local<v8::Value> frameworkValue;
        context->Global()
            ->Get(context, v8::String::NewFromUtf8(_isolate, "Framework").ToLocalChecked())
            .ToLocal(&frameworkValue);
        return frameworkValue.As<v8::Object>();
    }

    std::string V8Engine::FormatException(v8::TryCatch &tryCatch) {
        v8::HandleScope handleScope(_isolate);
        v8::String::Utf8Value exception(_isolate, tryCatch.Exception());

        std::string message = *exception ? *exception : "Unknown error";

        v8::Local<v8::Message> msg = tryCatch.Message();
        if (!msg.IsEmpty()) {
            v8::String::Utf8Value filename(_isolate, msg->GetScriptResourceName());
            int linenum = msg->GetLineNumber(_context.Get(_isolate)).FromMaybe(-1);

            message = std::string(*filename ? *filename : "<unknown>") + ":" + std::to_string(linenum) + ": " + message;
        }

        return message;
    }

} // namespace Framework::Scripting
