#include "engine.h"

#include <logging/logger.h>

namespace Framework::Scripting {
    Engine::~Engine() {
        if (_resourceManager) {
            _resourceManager->UnloadAll();
            delete _resourceManager;
        }
    }

    EngineError Engine::Init(SDKRegisterCallback cb) {
        // Define the arguments to be passed to the node instance
        std::vector<std::string> argv = {"main.exe"};
        std::vector<std::string> eav  = {};
        std::vector<std::string> errors;

        // Initialize the node with the provided arguments
        int exitCode = node::InitializeNodeWithArgs(&argv, &eav, &errors);
        if (exitCode != 0) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize node {}", exitCode);
            return ENGINE_NODE_INIT_FAILED;
        }

        // Create the multi isolate platform on a single thread
        _platform = node::MultiIsolatePlatform::Create(1, nullptr);
        if (!_platform) {
            Logging::GetInstance()->Get(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize the node multi isolate platform");
            return ENGINE_PLATFORM_INIT_FAILED;
        }

        // Create the unique V8 platform
        v8::V8::InitializePlatform(_platform.get());
        if (!v8::V8::Initialize()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize the V8 engine");
            return ENGINE_V8_INIT_FAILED;
        }

        // Initialize the event loop
        if (uv_loop_init(&_uvLoop) != 0) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize the event loop");
            return ENGINE_UV_LOOP_INIT_FAILED;
        }

        _allocator = node::ArrayBufferAllocator::Create();

        // Create the isolate instance
        _isolate = node::NewIsolate(_allocator, &_uvLoop, _platform.get());
        if (!_isolate) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize the node isolate");
            return ENGINE_ISOLATE_ALLOCATION_FAILED;
        }

        v8::Locker isolateLocker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);

        // Create the isolate data instance
        _isolateData = node::CreateIsolateData(_isolate, &_uvLoop, _platform.get(), _allocator.get());
        if (!_isolateData) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to create the isolate data");
            return ENGINE_ISOLATE_DATA_ALLOCATION_FAILED;
        }

        // Initialize the error reporting and console delegate
        _isolate->SetFatalErrorHandler([](const char *location, const char *message) {
            if (location) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("{} {}", location, message);
            } else {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("{}", message);
            }
        });
        _isolate->AddMessageListener([](v8::Local<v8::Message> message, v8::Local<v8::Value> error) {
            auto isolate = v8::Isolate::GetCurrent();
            v8::HandleScope scope(isolate);

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Context::Scope context_scope(context);

            v8::String::Utf8Value res(isolate, message->GetScriptResourceName());
            v8::String::Utf8Value msg(isolate, message->Get());

            v8::Local<v8::Object> err_obj    = error->ToObject(context).ToLocalChecked();
            v8::Local<v8::Value> trace_value = err_obj->Get(context, v8::String::NewFromUtf8(isolate, "stack").ToLocalChecked()).ToLocalChecked();
            v8::String::Utf8Value trace(isolate, trace_value->ToString(context).ToLocalChecked());

            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("{}: {}", *res, *msg);
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("{}", *trace);
        });

        v8::HandleScope isolateHandleScope(_isolate);
        v8::Local<v8::ObjectTemplate> globalObjTemplate = v8::ObjectTemplate::New(_isolate);
        _globalObjectTemplate.Reset(_isolate, globalObjTemplate);

        // Load the resources
        _resourceManager = new ResourceManager(this);
        _resourceManager->LoadAll(cb);
        _resourceManager->UnloadAll();
        _resourceManager->LoadAll(cb);
        return ENGINE_NONE;
    }

    EngineError Engine::Shutdown() {
        if (!_platform || !_isolate) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to acquire platform or isolate");
            return ENGINE_ISOLATE_NULL;
        }

        // Acquire resources
        v8::Locker lock(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);

        // Unload the resources
        if (_resourceManager) {
            _resourceManager->UnloadAll();
        }

        node::FreeIsolateData(_isolateData);
        _isolateData = nullptr;

        _platform->UnregisterIsolate(_isolate);
        _isolate->Dispose();
        _isolate = nullptr;

        v8::V8::Dispose();
        v8::V8::ShutdownPlatform();
        node::FreePlatform(_platform.release());

        return ENGINE_NONE;
    }

    void Engine::Update() {
        if (!_platform || !_isolate) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to acquire platform or isolate");
            return;
        }

        // Lock and isolate
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::SealHandleScope seal(_isolate);

        // Tick the platform
        uv_run(&_uvLoop, UV_RUN_NOWAIT);
        while (_platform->FlushForegroundTasks(_isolate)) {}

        // Tick all resources
        for (auto resource : _resourceManager->GetAllResources()) { resource.second->Update(); }
    }
} // namespace Framework::Scripting
