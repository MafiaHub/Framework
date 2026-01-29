#include "js_module.h"

#include <logging/logger.h>

#include <scripting/js/builtins/builtins.h>
#include <scripting/js/builtins/events.h>
#include <scripting/js/builtins/messages.h>
#include <scripting/js/builtins/console.h>
#include <scripting/js/builtins/imports.h>

namespace Framework::Integrations::Server::Scripting {

    JSServerScriptingModule::JSServerScriptingModule(std::shared_ptr<World::ServerEngine> world)
        : _world(world) {
        _nodeEngine = std::make_unique<Framework::Scripting::JS::NodeEngine>();
    }

    JSServerScriptingModule::~JSServerScriptingModule() {
        PreShutdown();
        Shutdown();
    }

    bool JSServerScriptingModule::Init(Framework::Scripting::JS::Engine::SDKRegisterCallback sdkCallback) {
        // Set the SDK callback before initialization
        if (sdkCallback) {
            _nodeEngine->SetSDKRegisterCallback(sdkCallback);
        }

        // Initialize the Node.js engine
        if (!_nodeEngine->Init()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Failed to initialize Node.js engine: {}", _nodeEngine->GetLastError());
            return false;
        }

        // Initialize ResourceManager with server-side config
        Framework::Scripting::JS::JSResourceManagerConfig config;
        config.resourcesPath = _resourcesPath;
        config.isClient = false;
        config.cascadeStopDependents = true;

        _resourceManager = std::make_unique<Framework::Scripting::JS::JSResourceManager>(
            _nodeEngine.get(), config);

        // Register Framework SDK bindings
        RegisterFrameworkBindings();

        // Initialize Framework SDK in the engine
        _nodeEngine->InitFrameworkSDK();

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(
            "JS Server scripting module initialized with Node.js engine");

        return true;
    }

    void JSServerScriptingModule::RegisterFrameworkBindings() {
        if (!_nodeEngine || !_nodeEngine->IsInitialized()) {
            return;
        }

        v8::Isolate *isolate = _nodeEngine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = _nodeEngine->GetContext();
        v8::Context::Scope contextScope(context);

        // Get or create Framework global object
        v8::Local<v8::Object> global = context->Global();
        v8::Local<v8::String> frameworkKey = v8::String::NewFromUtf8(isolate, "Framework").ToLocalChecked();

        v8::Local<v8::Object> frameworkObj;
        v8::Local<v8::Value> existingFramework;
        if (global->Get(context, frameworkKey).ToLocal(&existingFramework) && existingFramework->IsObject()) {
            frameworkObj = existingFramework.As<v8::Object>();
        } else {
            frameworkObj = v8::Object::New(isolate);
            global->Set(context, frameworkKey, frameworkObj).Check();
        }

        // Register math type builtins
        Framework::Scripting::JS::Builtins::RegisterAll(isolate, frameworkObj);

        // Register communication APIs
        Framework::Scripting::JS::Events::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::JS::Messages::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::JS::Imports::Register(isolate, context, frameworkObj, _resourceManager.get());

        // Register console override
        Framework::Scripting::JS::Console::Register(isolate, context, _resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Registered Framework JS bindings");
    }

    bool JSServerScriptingModule::PreShutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
        }
        return true;
    }

    bool JSServerScriptingModule::Shutdown() {
        _resourceManager.reset();

        if (_nodeEngine) {
            _nodeEngine->Shutdown();
            _nodeEngine.reset();
        }

        return true;
    }

    void JSServerScriptingModule::Update() {
        if (_nodeEngine && _nodeEngine->IsInitialized()) {
            // Process Node.js event loop
            _nodeEngine->Tick();
        }

        if (_resourceManager) {
            // Process scheduled restarts
            _resourceManager->ProcessScheduledRestarts();
        }

        // Process pending message responses
        if (_nodeEngine && _nodeEngine->IsInitialized() && _resourceManager) {
            v8::Isolate *isolate = _nodeEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _nodeEngine->GetContext();
            v8::Context::Scope contextScope(context);

            Framework::Scripting::JS::Messages::ProcessPendingResponses(isolate, context);
        }
    }

    void JSServerScriptingModule::SetResourcesPath(const std::string &path) {
        _resourcesPath = path;
        if (_resourceManager) {
            Framework::Scripting::JS::JSResourceManagerConfig config = _resourceManager->GetConfig();
            config.resourcesPath = path;
            _resourceManager->SetConfig(config);
        }
    }

    std::vector<JSClientResourceInfo> JSServerScriptingModule::GetClientResourceList() const {
        std::vector<JSClientResourceInfo> result;

        if (!_resourceManager) {
            return result;
        }

        // Get all resources that have client entry points
        auto resourceNames = _resourceManager->GetAllResourceNames();
        for (const auto &name : resourceNames) {
            const auto *resource = _resourceManager->GetResource(name);
            if (!resource) {
                continue;
            }

            // Only include resources that have client entry points
            if (!resource->GetClientEntryPoint().empty()) {
                JSClientResourceInfo info;
                info.name = resource->GetName();
                info.version = resource->GetVersion();
                result.push_back(info);
            }
        }

        return result;
    }

    bool JSServerScriptingModule::StartAllResources() {
        if (!_resourceManager) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("JS ResourceManager not initialized");
            return false;
        }

        // Discover all resources in the resources path
        size_t discovered = _resourceManager->DiscoverResources();
        if (discovered == 0) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(
                "No JS resources discovered in: {}", _resourcesPath);
            return true; // Not an error, just no resources
        }

        // Start all discovered resources
        auto result = _resourceManager->StartAll();
        if (!result.success) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Failed to start JS resources: {}", result.error);
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(
            "Started {} JS resource(s)", result.affectedResources.size());
        return true;
    }

} // namespace Framework::Integrations::Server::Scripting
