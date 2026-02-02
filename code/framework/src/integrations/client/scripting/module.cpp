#include "module.h"

#include <logging/logger.h>

#include <scripting/builtins/builtins.h>
#include <scripting/builtins/events.h>
#include <scripting/builtins/messages.h>
#include <scripting/builtins/console.h>
#include <scripting/builtins/imports.h>

#include <filesystem>

namespace Framework::Integrations::Client::Scripting {

    ClientScriptingModule::ClientScriptingModule(std::shared_ptr<World::ClientEngine> world)
        : _world(world) {
        // Create Node.js engine with sandbox mode enabled for client security
        Framework::Scripting::NodeEngineOptions options;
        options.sandboxed = true;
        options.processName = "mafiahub-client";
        _nodeEngine = std::make_unique<Framework::Scripting::NodeEngine>(options);
    }

    ClientScriptingModule::~ClientScriptingModule() {
        Shutdown();
    }

    bool ClientScriptingModule::Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback) {
        // Check if engine is already initialized (e.g., after Reset())
        bool engineAlreadyInitialized = _nodeEngine && _nodeEngine->IsInitialized();

        // Set the SDK callback before initialization (only on first init)
        if (!engineAlreadyInitialized && sdkCallback) {
            _nodeEngine->SetSDKRegisterCallback(sdkCallback);
        }

        // Initialize the Node.js engine (sandboxed) - no-op if already initialized
        if (!_nodeEngine->Init()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Failed to initialize Node.js engine (sandboxed): {}", _nodeEngine->GetLastError());
            return false;
        }

        // Initialize ResourceManager with client-side config
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = _resourceCachePath;
        config.isClient = true;
        config.cascadeStopDependents = true;

        _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(
            _nodeEngine.get(), config);

        // Register Framework SDK bindings and call SDK callback only on first init
        if (!engineAlreadyInitialized) {
            RegisterFrameworkBindings();

            if (!_nodeEngine->InitFrameworkSDK()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                    "Failed to initialize Framework SDK: {}", _nodeEngine->GetLastError());
                _resourceManager.reset();
                return false;
            }
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(
            "Client scripting module initialized with Node.js engine (sandboxed)");

        return true;
    }

    void ClientScriptingModule::RegisterFrameworkBindings() {
        if (!_nodeEngine || !_nodeEngine->IsInitialized()) {
            return;
        }

        v8::Isolate *isolate = _nodeEngine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = _nodeEngine->GetContext();
        v8::Context::Scope contextScope(context);

        // Get Framework global object (created by NodeEngine)
        v8::Local<v8::Object> frameworkObj = _nodeEngine->GetFrameworkObject();
        v8::Local<v8::Object> global = context->Global();

        // Register math type builtins (on global for parity with server)
        Framework::Scripting::Builtins::RegisterAll(isolate, global);

        // Register communication APIs
        _resourceManager->GetEvents().Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Messages::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::Imports::Register(isolate, context, frameworkObj, _resourceManager.get());

        // Register console override
        Framework::Scripting::Console::Register(isolate, context, _resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Registered Framework bindings (client, sandboxed)");
    }

    bool ClientScriptingModule::Shutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
            _resourceManager.reset();
        }

        if (_nodeEngine) {
            _nodeEngine->Shutdown();
            _nodeEngine.reset();
        }

        _serverResourceList.clear();
        _resourcesSynced = false;

        return true;
    }

    void ClientScriptingModule::Reset() {
        // Stop all resources but keep the engine running
        if (_resourceManager) {
            _resourceManager->StopAll();
            _resourceManager.reset();
        }

        _serverResourceList.clear();
        _resourcesSynced = false;

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Client scripting module reset");
    }

    void ClientScriptingModule::Update() {
        if (_resourceManager) {
            _resourceManager->ProcessScheduledRestarts();
        }

        // Process Node.js event loop and pending message responses
        if (_nodeEngine && _nodeEngine->IsInitialized()) {
            // Tick processes libuv events (timers, I/O callbacks, etc.)
            _nodeEngine->Tick();

            if (_resourceManager) {
                v8::Isolate *isolate = _nodeEngine->GetIsolate();
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                v8::HandleScope handleScope(isolate);
                v8::Local<v8::Context> context = _nodeEngine->GetContext();
                v8::Context::Scope contextScope(context);

                Framework::Scripting::Messages::ProcessPendingResponses(isolate, context);
            }
        }
    }

    void ClientScriptingModule::SetResourceCachePath(const std::string &path) {
        _resourceCachePath = path;
        if (_resourceManager) {
            Framework::Scripting::ResourceManagerConfig config = _resourceManager->GetConfig();
            config.resourcesPath = path;
            _resourceManager->SetConfig(config);
        }
    }

    void ClientScriptingModule::OnServerResourceList(const std::vector<ServerResourceInfo> &resources) {
        _serverResourceList = resources;
        _resourcesSynced = false;

        if (resources.empty()) {
            _resourcesSynced = true;
            if (_onResourceSyncComplete) {
                _onResourceSyncComplete(true);
            }
            return;
        }

        // Check which resources need to be downloaded
        bool anyMissing = false;
        for (const auto &resource : resources) {
            std::string resourcePath = GetResourcePath(resource.name);

            // Check if resource exists locally
            if (!std::filesystem::exists(resourcePath)) {
                anyMissing = true;
                if (_onResourceDownloadNeeded) {
                    _onResourceDownloadNeeded(resource.name, resource.version);
                }
            }
        }

        // If all resources already exist locally, mark as synced immediately
        if (!anyMissing) {
            _resourcesSynced = true;
            if (_onResourceSyncComplete) {
                _onResourceSyncComplete(true);
            }
        }
    }

    void ClientScriptingModule::OnResourceDownloaded(const std::string &resourceName) {
        // Discover the downloaded resource
        std::string resourcePath = GetResourcePath(resourceName);
        if (_resourceManager) {
            _resourceManager->DiscoverResource(resourcePath);
        }

        // Check if all resources have been downloaded
        bool allDownloaded = true;
        for (const auto &resource : _serverResourceList) {
            std::string path = GetResourcePath(resource.name);
            if (!std::filesystem::exists(path)) {
                allDownloaded = false;
                break;
            }
        }

        if (allDownloaded && !_resourcesSynced) {
            _resourcesSynced = true;
            if (_onResourceSyncComplete) {
                _onResourceSyncComplete(true);
            }
        }
    }

    bool ClientScriptingModule::StartAllResources() {
        if (!_resourceManager) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("ResourceManager not initialized");
            return false;
        }

        // Discover all resources in the cache path
        size_t discovered = _resourceManager->DiscoverResources();
        if (discovered == 0) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(
                "No JS resources discovered in: {}", _resourceCachePath);
            return true; // Not an error, just no resources
        }

        // Start all discovered resources
        auto result = _resourceManager->StartAll();
        if (!result.success) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Failed to start resources: {}", result.error);
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(
            "Started {} resource(s) on client", result.affectedResources.size());
        return true;
    }

    void ClientScriptingModule::StopAllResources() {
        if (_resourceManager) {
            _resourceManager->StopAll();
        }
    }

    std::string ClientScriptingModule::GetResourcePath(const std::string &resourceName) const {
        return (std::filesystem::path(_resourceCachePath) / resourceName).string();
    }

} // namespace Framework::Integrations::Client::Scripting
