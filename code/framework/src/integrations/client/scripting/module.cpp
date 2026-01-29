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
        _v8Engine = std::make_unique<Framework::Scripting::V8Engine>();
    }

    ClientScriptingModule::~ClientScriptingModule() {
        Shutdown();
    }

    bool ClientScriptingModule::Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback) {
        // Set the SDK callback before initialization
        if (sdkCallback) {
            _v8Engine->SetSDKRegisterCallback(sdkCallback);
        }

        // Initialize the V8 engine
        if (!_v8Engine->Init()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Failed to initialize V8 engine: {}", _v8Engine->GetLastError());
            return false;
        }

        // Initialize ResourceManager with client-side config
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = _resourceCachePath;
        config.isClient = true;
        config.cascadeStopDependents = true;

        _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(
            _v8Engine.get(), config);

        // Register Framework SDK bindings
        RegisterFrameworkBindings();

        // Initialize Framework SDK in the engine
        if (!_v8Engine->InitFrameworkSDK()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Failed to initialize Framework SDK: {}", _v8Engine->GetLastError());
            _resourceManager.reset();
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(
            "Client scripting module initialized with V8 engine");

        return true;
    }

    void ClientScriptingModule::RegisterFrameworkBindings() {
        if (!_v8Engine || !_v8Engine->IsInitialized()) {
            return;
        }

        v8::Isolate *isolate = _v8Engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = _v8Engine->GetContext();
        v8::Context::Scope contextScope(context);

        // Get Framework global object (created by V8Engine)
        v8::Local<v8::Object> frameworkObj = _v8Engine->GetFrameworkObject();
        v8::Local<v8::Object> global = context->Global();

        // Register math type builtins (on global for parity with server)
        Framework::Scripting::Builtins::RegisterAll(isolate, global);

        // Register communication APIs
        _resourceManager->GetEvents().Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Messages::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::Imports::Register(isolate, context, frameworkObj, _resourceManager.get());

        // Register console override
        Framework::Scripting::Console::Register(isolate, context, _resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Registered Framework bindings (client)");
    }

    bool ClientScriptingModule::Shutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
            _resourceManager.reset();
        }

        if (_v8Engine) {
            _v8Engine->Shutdown();
            _v8Engine.reset();
        }

        _serverResourceList.clear();
        _resourcesSynced = false;

        return true;
    }

    void ClientScriptingModule::Update() {
        if (_resourceManager) {
            _resourceManager->ProcessScheduledRestarts();
        }

        // Process pending message responses
        if (_v8Engine && _v8Engine->IsInitialized() && _resourceManager) {
            v8::Isolate *isolate = _v8Engine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _v8Engine->GetContext();
            v8::Context::Scope contextScope(context);

            Framework::Scripting::Messages::ProcessPendingResponses(isolate, context);
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
