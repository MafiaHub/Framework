#include "module.h"

#include <logging/logger.h>

#include <scripting/builtins/builtins.h>
#include <scripting/builtins/events.h>
#include <scripting/builtins/messages.h>
#include <scripting/builtins/console.h>
#include <scripting/builtins/imports.h>
#include <scripting/builtins/exports.h>

namespace {
    // Global pointer for internal callbacks (set during RegisterFrameworkBindings)
    Framework::Scripting::ResourceManager *g_resourceManager = nullptr;

    // Called from ES module loader after import completes
    void EmitResourceStartCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        v8::Isolate *isolate = args.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        if (args.Length() < 1 || !args[0]->IsString()) {
            return;
        }

        std::string resourceName = v8pp::from_v8<std::string>(isolate, args[0]);

        if (g_resourceManager) {
            g_resourceManager->SetCurrentResourceContext(resourceName);
            g_resourceManager->DecrementPendingLoads();
        }

        std::vector<v8::Local<v8::Value>> eventArgs;
        eventArgs.push_back(args[0]);
        if (g_resourceManager) {
            g_resourceManager->GetEvents().EmitReserved(isolate, context, "resourceStart", eventArgs);
        }

        if (g_resourceManager) {
            g_resourceManager->SetCurrentResourceContext("");
        }
    }

    // Called from ES module loader on load error
    void OnLoadErrorCallback(const v8::FunctionCallbackInfo<v8::Value> &args) {
        if (g_resourceManager) {
            g_resourceManager->DecrementPendingLoads();
        }
    }

    // Register Framework.__internal functions for ES module lifecycle
    void RegisterInternalFunctions(v8::Isolate *isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> frameworkObj) {
        v8::Local<v8::Object> internalObj = v8::Object::New(isolate);
        frameworkObj->Set(context, v8pp::to_v8(isolate, "__internal"), internalObj).Check();

        v8::Local<v8::FunctionTemplate> emitTmpl = v8::FunctionTemplate::New(isolate, EmitResourceStartCallback);
        internalObj->Set(context, v8pp::to_v8(isolate, "emitResourceStart"),
                         emitTmpl->GetFunction(context).ToLocalChecked()).Check();

        v8::Local<v8::FunctionTemplate> errorTmpl = v8::FunctionTemplate::New(isolate, OnLoadErrorCallback);
        internalObj->Set(context, v8pp::to_v8(isolate, "onLoadError"),
                         errorTmpl->GetFunction(context).ToLocalChecked()).Check();
    }
} // anonymous namespace

namespace Framework::Integrations::Server::Scripting {

    ServerScriptingModule::ServerScriptingModule(std::shared_ptr<World::ServerEngine> world)
        : _world(world) {
        // Create Node.js engine without sandbox for full server capabilities
        Framework::Scripting::NodeEngineOptions options;
        options.sandboxed = false;
        options.processName = "mafiahub-server";
        _nodeEngine = std::make_unique<Framework::Scripting::NodeEngine>(options);
    }

    ServerScriptingModule::~ServerScriptingModule() {
        PreShutdown();
        Shutdown();
    }

    bool ServerScriptingModule::Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback) {
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
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = _resourcesPath;
        config.isClient = false;
        config.cascadeStopDependents = true;

        _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(
            _nodeEngine.get(), config);

        // Register Framework SDK bindings
        RegisterFrameworkBindings();

        // Initialize Framework SDK in the engine
        _nodeEngine->InitFrameworkSDK();

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(
            "JS Server scripting module initialized with Node.js engine");

        return true;
    }

    void ServerScriptingModule::RegisterFrameworkBindings() {
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

        // Register math type builtins on global for direct access (Vector3, Color, etc.)
        Framework::Scripting::Builtins::RegisterAll(isolate, global);

        // Register communication APIs
        _resourceManager->GetEvents().Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Messages::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::Imports::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::Exports::Register(isolate, context, frameworkObj, _resourceManager.get());

        // Register console override
        Framework::Scripting::Console::Register(isolate, context, _resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Registered Framework JS bindings");

        // Register internal functions for ES module lifecycle
        g_resourceManager = _resourceManager.get();
        RegisterInternalFunctions(isolate, context, frameworkObj);
    }

    bool ServerScriptingModule::PreShutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
        }
        g_resourceManager = nullptr;
        return true;
    }

    bool ServerScriptingModule::Shutdown() {
        _resourceManager.reset();
        g_resourceManager = nullptr;

        if (_nodeEngine) {
            _nodeEngine->Shutdown();
            _nodeEngine.reset();
        }

        return true;
    }

    void ServerScriptingModule::Update() {
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

            Framework::Scripting::Messages::ProcessPendingResponses(isolate, context);
        }
    }

    void ServerScriptingModule::SetResourcesPath(const std::string &path) {
        _resourcesPath = path;
        if (_resourceManager) {
            Framework::Scripting::ResourceManagerConfig config = _resourceManager->GetConfig();
            config.resourcesPath = path;
            _resourceManager->SetConfig(config);
        }
    }

    std::vector<ClientResourceInfo> ServerScriptingModule::GetClientResourceList() const {
        std::vector<ClientResourceInfo> result;

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
                ClientResourceInfo info;
                info.name = resource->GetName();
                info.version = resource->GetVersion();
                result.push_back(info);
            }
        }

        return result;
    }

    bool ServerScriptingModule::StartAllResources() {
        if (!_resourceManager) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("ResourceManager not initialized");
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

    bool ServerScriptingModule::HasPendingLoads() const {
        return _resourceManager && _resourceManager->HasPendingLoads();
    }

} // namespace Framework::Integrations::Server::Scripting
