/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "module.h"

#include <logging/logger.h>

#include <scripting/bind/lua_definitions.h>
#include <scripting/builtins/builtins.h>
#include <scripting/builtins/console.h>
#include <scripting/builtins/events.h>
#include <scripting/builtins/execution_environment.h>
#include <scripting/builtins/exports.h>
#include <scripting/builtins/imports.h>
#include <scripting/builtins/messages.h>
#include <scripting/engine_scope.h>
#include <scripting/event_metadata.h>
#include <scripting/scripting_catalog.h>
#include <scripting/timer_metadata.h>

#include "builtins/chat.h"
#include "builtins/discord.h"
#include "builtins/keybinds.h"
#include "builtins/nametags.h"
#include "builtins/voice.h"
#include "builtins/web.h"

#include <utils/vfs.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Framework::Integrations::Client::Scripting {
    namespace {
        bool IsResourceAvailableLocally(const std::string &resourcePath) {
            return Framework::Utils::Vfs::Get().Contains(resourcePath + "/package.json");
        }

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

        // Lua scripts come from the server as the JavaScript ones do: sandboxed, with the memory and the time of
        // a client state.
        _lua = std::make_unique<Framework::Scripting::LuaClientHost>(Framework::Scripting::LuaEngineOptions::Client());
        // The builtins only a client has, next to the shared ones of every state.
        _lua->AddRuntimeSetup([](Framework::Scripting::LuaRuntime &runtime) {
            Builtins::Web::RegisterLua(runtime);
            Builtins::Keybinds::RegisterLua(runtime);
            Builtins::Chat::RegisterLua(runtime);
            Builtins::Discord::RegisterLua(runtime);
            Builtins::Voice::RegisterLua(runtime);
            Builtins::Nametags::RegisterLua(runtime);
        });
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

        // Resources are served from the mounted packages, not the cache directory the encrypted
        // blobs sit in.
        _engine->SetModuleRootPath(Framework::Utils::Vfs::kResourceMountRoot);

        // Initialize the V8 engine - no-op if already initialized
        if (_engine->Init() != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to initialize V8 engine: {}", _engine->GetLastError());
            return Framework::Scripting::ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
        }

        // Initialize ResourceManager with client-side config
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath         = Framework::Utils::Vfs::kResourceMountRoot;
        config.isClient              = true;
        config.cascadeStopDependents = true;

        if (_resourceManager) {
            _resourceManager->SetConfig(config);
        }
        else {
            _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(_engine.get(), config);
        }

        // The Lua engine is up before the resource manager can start a resource in it.
        if (_lua->Init(*_resourceManager, _modVersion) != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
            return Framework::Scripting::ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
        }

        // Web views and key binds created by a resource die with it (single callback slot).
        _resourceManager->AddOnResourceStopped([manager = _resourceManager.get()](const std::string &resourceName) {
            Builtins::Web::CleanupResource(resourceName);
            Builtins::Keybinds::CleanupResource(manager, resourceName);
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
        if (!Framework::Scripting::Bind::ExportLuaDefinitionsFromEnvironment(Framework::Scripting::Builtins::SharedDefinitions({&Builtins::Web::Definition().Describe(), &Builtins::Keybinds::Definition().Describe(), &Builtins::Chat::Definition().Describe(),
                                                                                 &Builtins::Discord::Definition().Describe(), &Builtins::Voice::Definition().Describe(), &Builtins::Nametags::Definition().Describe()}),
                Framework::Scripting::Bind::Side::Client, "FRAMEWORK_SCRIPTING_LUA_DEFINITIONS")) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to export the Framework client Lua definitions");
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Client scripting module initialized with the standalone V8 and Lua engines");

        _initialized = true;
        return Framework::Scripting::ScriptingError::SCRIPTING_NONE;
    }

    void ClientScriptingModule::RegisterFrameworkBindings() {
        if (!_engine || !_engine->IsInitialized()) {
            return;
        }

        Framework::Scripting::EngineScope scope(_engine.get());
        v8::Isolate *isolate           = scope.Isolate();
        v8::Local<v8::Context> context = scope.Context();

        v8::Local<v8::Object> global = context->Global();

        Framework::Scripting::SetScriptingCatalog(isolate, "framework-client");
        Framework::Scripting::SetScriptingEnvironment(isolate, /*isClient*/ true);

        // The events the framework raises; a mod's catalog blends them into its own EventMap.
        Framework::Scripting::RegisterEventMetadata(Framework::Scripting::GetScriptingCatalog(isolate));

        // The engine installs the timers without a binding; declare them for it.
        Framework::Scripting::RegisterTimerMetadata(Framework::Scripting::GetScriptingCatalog(isolate), /*isClient*/ true);

        // Every builtin registers at the global root (new Vector3, not new Core.Vector3).
        Framework::Scripting::Builtins::RegisterValueTypes(isolate, global);

        _resourceManager->GetEvents().Register(isolate, context, global, _resourceManager.get(), /*isClient*/ true);
        Framework::Scripting::Builtins::Messages::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Exports::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Imports::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Console::Register(isolate, context, _resourceManager.get());
        Framework::Scripting::Builtins::ExecutionEnvironment::Register(isolate, context, global, true, _modVersion);

        // Client-only surface.
        Builtins::Web::Register(isolate, context, global, _resourceManager.get());
        Builtins::Keybinds::Register(isolate, context, global, _resourceManager.get());
        Builtins::Chat::Register(isolate, context, global, _resourceManager.get());
        Builtins::Discord::Register(isolate, context, global, _resourceManager.get());
        Builtins::Voice::Register(isolate, context, global, _resourceManager.get());
        Builtins::Nametags::Register(isolate, context, global, _resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Registered Framework bindings (client, V8 engine)");
    }

    void ClientScriptingModule::Shutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
        }

        // What these two kept names the manager: let go of before it is destroyed.
        Builtins::Web::Shutdown();
        Builtins::Keybinds::Shutdown();
        _resourceManager.reset();

        // After the resources, whose Lua states it owns, and before the V8 engine.
        _lua->Shutdown();

        // Releases V8 globals; must precede engine shutdown (isolate disposal)
        Builtins::Discord::Shutdown();
        Builtins::Nametags::Shutdown();

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

        // No Lua state outlives the connection its scripts came over.
        _lua->Shutdown();

        Builtins::Web::Shutdown();
        Builtins::Keybinds::Shutdown();
        Builtins::Discord::Shutdown();
        Builtins::Nametags::Shutdown();

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
            // What the handlers the builtins keep failed with since the last update: here, outside any script.
            _resourceManager->ProcessHandlerFailures();
            _resourceManager->ProcessScheduledRestarts();
        }

        // Lua timers and sleeps, and what they raised.
        _lua->Update();

        // Process V8 timers, microtasks, and pending message responses
        if (_engine && _engine->IsInitialized()) {
            _engine->Tick();

            if (_resourceManager) {
                Framework::Scripting::EngineScope scope(_engine.get());
                v8::Isolate *isolate           = scope.Isolate();
                v8::Local<v8::Context> context = scope.Context();

                Framework::Scripting::Builtins::Messages::ProcessPendingResponses(isolate, context);
            }

            // Key binds enter the runtime of each handler; Web self-scopes the isolate.
            Builtins::Keybinds::Update();
            Builtins::Web::Update();
        }
        else {
            // Without JavaScript the replies owed to Lua are delivered all the same.
            Framework::Scripting::Builtins::Messages::ProcessPendingResponses();
        }
    }

    void ClientScriptingModule::SetResourceCachePath(const std::string &path) {
        // A real directory holding the encrypted blobs; everything downstream addresses the
        // decrypted contents through the virtual mount root instead.
        _resourceCachePath = path;
        if (_engine) {
            _engine->SetModuleRootPath(Framework::Utils::Vfs::kResourceMountRoot);
        }
        if (_resourceManager) {
            Framework::Scripting::ResourceManagerConfig config = _resourceManager->GetConfig();
            config.resourcesPath                               = Framework::Utils::Vfs::kResourceMountRoot;
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

            if (!IsResourceAvailableLocally(resourcePath)) {
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
            if (!IsResourceAvailableLocally(path)) {
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
        // A verified package can still contain an invalid manifest. The
        // announced set must be discoverable before startup can succeed.
        for (const auto &resource : _serverResourceList) {
            if (!_resourceManager->HasResource(resource.name)) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Announced client resource '{}' could not be discovered", resource.name);
                return false;
            }
        }
        if (discovered == 0) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("No resources discovered in: {}", _resourceCachePath);
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
        return Framework::Utils::Vfs::ResourcePath(resourceName);
    }

} // namespace Framework::Integrations::Client::Scripting
