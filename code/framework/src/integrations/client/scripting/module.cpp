/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "module.h"

#include <logging/logger.h>

#include <scripting/builtins/builtins.h>
#include <scripting/builtins/console.h>
#include <scripting/builtins/execution_environment.h>
#include <scripting/builtins/events.h>
#include <scripting/builtins/exports.h>
#include <scripting/builtins/imports.h>
#include <scripting/builtins/messages.h>
#include <scripting/scripting_catalog.h>

#include "builtins/chat.h"
#include "builtins/discord.h"
#include "builtins/keybinds.h"
#include "builtins/web.h"

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

    ClientScriptingModule::ClientScriptingModule() {
        // Create standalone V8 engine for client (no Node.js runtime)
        // moduleRootPath is set later in Init() or SetResourceCachePath()
        // when the actual resource cache path is known.
        Framework::Scripting::V8EngineOptions options;
        options.processName = "mafiahub-client";
        _engine             = std::make_unique<Framework::Scripting::V8Engine>(options);
    }

    ClientScriptingModule::~ClientScriptingModule() {
        Shutdown();
    }

    Framework::Scripting::ScriptingError ClientScriptingModule::Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback) {
        if (!_engine) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)
                ->error("Cannot initialize client scripting: engine is null (Shutdown() was called). "
                        "Create a new ClientScriptingModule instance instead.");
            return Framework::Scripting::ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
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
        if (_engine->Init() != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to initialize V8 engine: {}", _engine->GetLastError());
            return Framework::Scripting::ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
        }

        // Initialize ResourceManager with client-side config
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath         = _resourceCachePath;
        config.isClient              = true;
        config.cascadeStopDependents = true;

        if (_resourceManager) {
            _resourceManager->SetConfig(config);
        }
        else {
            _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(_engine.get(), config);
        }

        // Web views and key binds created by a resource die with it (single callback slot).
        _resourceManager->SetOnResourceStopped([](const std::string &resourceName) {
            Builtins::Web::CleanupResource(resourceName);
            Builtins::Keybinds::CleanupResource(resourceName);
        });

        RegisterFrameworkBindings();

        if (!engineAlreadyInitialized || _contextNeedsSDKRegistration) {
            if (!_engine->InitFrameworkSDK()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to initialize Framework SDK: {}", _engine->GetLastError());
                return Framework::Scripting::ScriptingError::SCRIPTING_SDK_INIT_FAILED;
            }
            _contextNeedsSDKRegistration = false;
        }

        if (!v8pp::metadata::export_catalog_from_environment("framework-client", "FRAMEWORK_SCRIPTING_API_METADATA")) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to export Framework client scripting API metadata");
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Client scripting module initialized with standalone V8 engine");

        _initialized = true;
        return Framework::Scripting::ScriptingError::SCRIPTING_NONE;
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

        v8::Local<v8::Object> global = context->Global();

        Framework::Scripting::SetScriptingCatalog(isolate, "framework-client");

        // Every builtin registers at the global root (new Vector3, not new Core.Vector3).
        Framework::Scripting::Builtins::RegisterValueTypes(isolate, global);

        _resourceManager->GetEvents().Register(isolate, context, global, _resourceManager.get(), /*isClient*/ true);
        Framework::Scripting::Builtins::Messages::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Exports::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Imports::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Console::Register(isolate, context, _resourceManager.get());
        Framework::Scripting::Builtins::ExecutionEnvironment::Register(isolate, context, global, true);

        // Client-only surface.
        Builtins::Web::Register(isolate, context, global, _resourceManager.get());
        Builtins::Keybinds::Register(isolate, context, global, _resourceManager.get());
        Builtins::Chat::Register(isolate, context, global, _resourceManager.get());
        Builtins::Discord::Register(isolate, context, global, _resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Registered Framework bindings (client, V8 engine)");
    }

    void ClientScriptingModule::Shutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
            _resourceManager.reset();
        }

        // Releases V8 globals; must precede engine shutdown (isolate disposal)
        Builtins::Web::Shutdown();
        Builtins::Keybinds::Shutdown();

        if (_engine) {
            Framework::Scripting::ClearScriptingCatalog(_engine->GetIsolate());
            _engine->Shutdown();
            _engine.reset();
        }

        _serverResourceList.clear();
        _resourcesSynced = false;

        Lifecycle::Shutdown();
    }

    void ClientScriptingModule::Reset() {
        if (_resourceManager) {
            _resourceManager->Reset();
        }

        Builtins::Web::Shutdown();
        Builtins::Keybinds::Shutdown();

        if (_engine && _engine->IsInitialized()) {
            if (!_engine->ResetContext()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to reset V8 context: {}", _engine->GetLastError());
            }
        }

        _contextNeedsSDKRegistration = true;
        _serverResourceList.clear();
        _resourcesSynced = false;

        Lifecycle::Shutdown();

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

                Framework::Scripting::Builtins::Messages::ProcessPendingResponses(isolate, context);
            }

            // Poll keys and dispatch key binds (self-scopes the isolate).
            Builtins::Keybinds::Update();
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
            config.resourcesPath                               = path;
            _resourceManager->SetConfig(config);
        }
    }

    void ClientScriptingModule::OnServerResourceList(const std::vector<ServerResourceInfo> &resources) {
        _serverResourceList.clear();
        _serverResourceList.reserve(resources.size());
        for (const auto &resource : resources) {
            if (!IsValidResourceName(resource.name)) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Ignoring invalid server resource name: '{}'", resource.name);
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
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Ignoring downloaded resource with invalid name: '{}'", resourceName);
            return;
        }

        if (std::none_of(_serverResourceList.begin(), _serverResourceList.end(), [&resourceName](const ServerResourceInfo &res) {
                return res.name == resourceName;
            })) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Ignoring downloaded resource not announced by server: '{}'", resourceName);
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
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("No JS resources discovered in: {}", _resourceCachePath);
            return true; // Not an error, just no resources
        }

        // Start all discovered resources
        auto result = _resourceManager->StartAll();
        if (!result) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to start resources: {}", result.GetError());
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Started {} resource(s) on client", result.GetValue().size());
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
