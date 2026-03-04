#include "module.h"

#include <logging/logger.h>

#include <scripting/builtins/builtins.h>
#include <scripting/builtins/events.h>
#include <scripting/builtins/exports.h>
#include <scripting/builtins/messages.h>
#include <scripting/builtins/console.h>
#include <scripting/builtins/imports.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Framework::Integrations::Client::Scripting {
    namespace {
        bool IsValidResourceName(const std::string &name) {
            if (name.empty()) {
                return false;
            }

            return std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '_' || c == '.';
            });
        }
    } // anonymous namespace

    ClientScriptingModule::ClientScriptingModule(std::shared_ptr<World::ClientEngine> world)
        : _world(world) {
        // Create standalone V8 engine for client (no Node.js runtime)
        // moduleRootPath is set later in Init() or SetResourceCachePath()
        // when the actual resource cache path is known.
        Framework::Scripting::V8EngineOptions options;
        options.processName = "mafiahub-client";
        _engine = std::make_unique<Framework::Scripting::V8Engine>(options);
    }

    ClientScriptingModule::~ClientScriptingModule() {
        Shutdown();
    }

    bool ClientScriptingModule::Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback) {
        if (!_engine) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Cannot initialize client scripting: engine is null (Shutdown() was called). "
                "Create a new ClientScriptingModule instance instead.");
            return false;
        }

        // Check if engine is already initialized (e.g., after Reset())
        bool engineAlreadyInitialized = _engine->IsInitialized();

        // Set the SDK callback before initialization (only on first init)
        if (!engineAlreadyInitialized && sdkCallback) {
            _engine->SetSDKRegisterCallback(sdkCallback);
        }

        // Sandbox require() paths to the resource cache directory
        _engine->SetModuleRootPath(std::filesystem::absolute(_resourceCachePath).string());

        // Initialize the V8 engine - no-op if already initialized
        if (!_engine->Init()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Failed to initialize V8 engine: {}", _engine->GetLastError());
            return false;
        }

        // Reusing an already initialized engine (reconnect/reset path):
        // clear stale module cache and loader callback data before loading
        // new resources.
        if (engineAlreadyInitialized) {
            _engine->ClearModuleCache();
        }

        // Initialize ResourceManager with client-side config
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = _resourceCachePath;
        config.isClient = true;
        config.cascadeStopDependents = true;

        _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(
            _engine.get(), config);

        // Register Framework SDK bindings for the new ResourceManager
        RegisterFrameworkBindings();

        // Initialize Framework SDK only on first init (not after Reset)
        if (!engineAlreadyInitialized) {
            if (!_engine->InitFrameworkSDK()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                    "Failed to initialize Framework SDK: {}", _engine->GetLastError());
                _resourceManager.reset();
                return false;
            }
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(
            "Client scripting module initialized with standalone V8 engine");

        _initialized = true;
        return true;
    }

    void ClientScriptingModule::RegisterFrameworkBindings() {
        if (!_engine || !_engine->IsInitialized()) {
            return;
        }

        v8::Isolate *isolate = _engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = _engine->GetContext();
        v8::Context::Scope contextScope(context);

        // Get Framework and Core global objects (created by V8Engine)
        v8::Local<v8::Object> frameworkObj = _engine->GetFrameworkObject();
        v8::Local<v8::Object> coreObj = _engine->GetCoreObject();

        // Register math type builtins on Core object
        Framework::Scripting::Builtins::RegisterAll(isolate, coreObj);

        // Register communication APIs
        _resourceManager->GetEvents().Register(isolate, context, coreObj, _resourceManager.get());
        Framework::Scripting::Messages::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::Exports::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::Imports::Register(isolate, context, frameworkObj, _resourceManager.get());

        // Register console override
        Framework::Scripting::Console::Register(isolate, context, _resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Registered Framework bindings (client, V8 engine)");
    }

    void ClientScriptingModule::Shutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
            _resourceManager.reset();
        }

        if (_engine) {
            _engine->Shutdown();
            _engine.reset();
        }

        _serverResourceList.clear();
        _resourcesSynced = false;

        Lifecycle::Shutdown();
    }

    void ClientScriptingModule::Reset() {
        // Stop all resources but keep the engine running
        if (_resourceManager) {
            _resourceManager->StopAll();
            _resourceManager.reset();
        }

        if (_engine && _engine->IsInitialized()) {
            _engine->ClearModuleCache();
        }

        _serverResourceList.clear();
        _resourcesSynced = false;

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Client scripting module reset");
    }

    void ClientScriptingModule::Update() {
        if (_resourceManager) {
            _resourceManager->ProcessScheduledRestarts();
        }

        // Process V8 timers, microtasks, and pending message responses
        if (_engine && _engine->IsInitialized()) {
            _engine->Tick();

            if (_resourceManager) {
                v8::Isolate *isolate = _engine->GetIsolate();
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                v8::HandleScope handleScope(isolate);
                v8::Local<v8::Context> context = _engine->GetContext();
                v8::Context::Scope contextScope(context);

                Framework::Scripting::Messages::ProcessPendingResponses(isolate, context);
            }
        }
    }

    void ClientScriptingModule::SetResourceCachePath(const std::string &path) {
        _resourceCachePath = path;
        // Update V8 engine's module root path so require() sandbox matches
        if (_engine) {
            _engine->SetModuleRootPath(std::filesystem::absolute(path).string());
        }
        if (_resourceManager) {
            Framework::Scripting::ResourceManagerConfig config = _resourceManager->GetConfig();
            config.resourcesPath = path;
            _resourceManager->SetConfig(config);
        }
    }

    void ClientScriptingModule::OnServerResourceList(const std::vector<ServerResourceInfo> &resources) {
        _serverResourceList.clear();
        _serverResourceList.reserve(resources.size());
        for (const auto &resource : resources) {
            if (!IsValidResourceName(resource.name)) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(
                    "Ignoring invalid server resource name: '{}'", resource.name);
                continue;
            }
            _serverResourceList.push_back(resource);
        }
        _resourcesSynced = false;

        if (_serverResourceList.empty()) {
            _resourcesSynced = true;
            if (_onResourceSyncComplete) {
                _onResourceSyncComplete(true);
            }
            return;
        }

        // Check which resources need to be downloaded
        bool anyMissing = false;
        for (const auto &resource : _serverResourceList) {
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
        if (!IsValidResourceName(resourceName)) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(
                "Ignoring downloaded resource with invalid name: '{}'", resourceName);
            return;
        }

        if (std::none_of(_serverResourceList.begin(), _serverResourceList.end(),
            [&resourceName](const ServerResourceInfo &res) { return res.name == resourceName; })) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn(
                "Ignoring downloaded resource not announced by server: '{}'", resourceName);
            return;
        }

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
