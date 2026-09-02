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
#include <scripting/builtins/chat.h>

#include "builtins/voice.h"
#include <scripting/builtins/console.h>
#include <scripting/builtins/execution_environment.h>
#include <scripting/builtins/events.h>
#include <scripting/builtins/exports.h>
#include <scripting/builtins/imports.h>
#include <scripting/builtins/messages.h>
#include <scripting/scripting_catalog.h>

namespace Framework::Integrations::Server::Scripting {

    ServerScriptingModule::ServerScriptingModule() {
        // Create Node.js engine without sandbox for full server capabilities
        Framework::Scripting::NodeEngineOptions options;
        options.sandboxed   = false;
        options.processName = "mafiahub-server";
#ifdef FW_NODE_INSPECTOR
        options.enableInspector = true;
        // inspectorPort keeps its NodeEngine default (the standard Node debug port).
#endif
        _nodeEngine = std::make_unique<Framework::Scripting::NodeEngine>(options);
    }

    ServerScriptingModule::~ServerScriptingModule() {
        PreShutdown();
        Shutdown();
    }

    Framework::Scripting::ScriptingError ServerScriptingModule::Init(Framework::Scripting::Engine::SDKRegisterCallback sdkCallback) {
        // Set the SDK callback before initialization
        if (sdkCallback) {
            _nodeEngine->SetSDKRegisterCallback(sdkCallback);
        }

        // Initialize the Node.js engine
        if (_nodeEngine->Init() != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to initialize Node.js engine: {}", _nodeEngine->GetLastError());
            return Framework::Scripting::ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
        }

        // Initialize ResourceManager with server-side config
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath         = _resourcesPath;
        config.isClient              = false;
        config.cascadeStopDependents = true;
        config.devMode               = _devMode;

        _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(_nodeEngine.get(), config);

        // Register Framework SDK bindings
        RegisterFrameworkBindings();

        // Initialize Framework SDK in the engine
        _nodeEngine->InitFrameworkSDK();

        if (!v8pp::metadata::export_catalog_from_environment("framework-server", "FRAMEWORK_SCRIPTING_API_METADATA")) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to export Framework server scripting API metadata");
        }

        // Install uncaught exception handler to prevent async errors from
        // crashing the server process and route them to the resource system
        {
            v8::Isolate *isolate = _nodeEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _nodeEngine->GetContext();
            v8::Context::Scope contextScope(context);

            _nodeEngine->InstallUncaughtExceptionHandler(_resourcesPath);
            _nodeEngine->InstallResourceTimerTracking();
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("JS Server scripting module initialized with Node.js engine");

        _initialized = true;
        return Framework::Scripting::ScriptingError::SCRIPTING_NONE;
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

        v8::Local<v8::Object> global = context->Global();
        Framework::Scripting::SetScriptingCatalog(isolate, "framework-server");
        Framework::Scripting::SetScriptingEnvironment(isolate, /*isClient*/ false);

        // Every builtin registers at the global root (new Vector3, not new Core.Vector3).
        Framework::Scripting::Builtins::RegisterValueTypes(isolate, global);

        _resourceManager->GetEvents().Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Messages::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Imports::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Exports::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Console::Register(isolate, context, _resourceManager.get());
        Framework::Scripting::Builtins::ExecutionEnvironment::Register(isolate, context, global, false);
        Framework::Scripting::Builtins::Chat::Register(isolate, global);
        Builtins::Voice::Register(isolate, global);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Registered Framework JS bindings");
    }

    bool ServerScriptingModule::PreShutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
        }
        return true;
    }

    void ServerScriptingModule::Shutdown() {
        _resourceManager.reset();

        if (_nodeEngine) {
            Framework::Scripting::ClearScriptingCatalog(_nodeEngine->GetIsolate());
            _nodeEngine->Shutdown();
            _nodeEngine.reset();
        }

        Lifecycle::Shutdown();
    }

    void ServerScriptingModule::Update() {
        if (_nodeEngine && _nodeEngine->IsInitialized()) {
            // Process Node.js event loop
            _nodeEngine->Tick();

            // Process uncaught errors captured during Tick().
            // Deferred to here so HandleResourceRuntimeError can safely
            // stop/restart resources outside of V8/libuv callbacks.
            if (_resourceManager) {
                auto errors = _nodeEngine->DrainPendingErrors();
                for (const auto &err : errors) {
                    _resourceManager->HandleResourceRuntimeError(err.resourceName, err.errorMessage);
                }
            }
        }

        if (_resourceManager) {
            // Process scheduled restarts
            _resourceManager->ProcessScheduledRestarts();
            // Dev-mode: poll resource files and hot-reload on change
            _resourceManager->ProcessFileWatch();
        }

        // Process pending message responses
        if (_nodeEngine && _nodeEngine->IsInitialized() && _resourceManager) {
            v8::Isolate *isolate = _nodeEngine->GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = _nodeEngine->GetContext();
            v8::Context::Scope contextScope(context);

            Framework::Scripting::Builtins::Messages::ProcessPendingResponses(isolate, context);
        }
    }

    void ServerScriptingModule::SetResourcesPath(const std::string &path) {
        _resourcesPath = path;
        if (_resourceManager) {
            Framework::Scripting::ResourceManagerConfig config = _resourceManager->GetConfig();
            config.resourcesPath                               = path;
            _resourceManager->SetConfig(config);
        }
    }

    void ServerScriptingModule::SetDevMode(bool enabled) {
        _devMode = enabled;
        if (_resourceManager) {
            Framework::Scripting::ResourceManagerConfig config = _resourceManager->GetConfig();
            config.devMode                                     = enabled;
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

            // Only running resources with a client entry point — a stopped
            // resource must not be handed to (re)connecting clients.
            if (resource->IsRunning() && resource->HasClientContent()) {
                ClientResourceInfo info;
                info.name    = resource->GetName();
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
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("No JS resources discovered in: {}", _resourcesPath);
            return true; // Not an error, just no resources
        }

        // Start all discovered resources
        auto result = _resourceManager->StartAll();
        if (!result) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to start JS resources: {}", result.GetError());
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Started {} JS resource(s)", result.GetValue().size());
        return true;
    }

} // namespace Framework::Integrations::Server::Scripting
