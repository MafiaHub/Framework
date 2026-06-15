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
#include <scripting/builtins/events.h>
#include <scripting/builtins/messages.h>
#include <scripting/builtins/console.h>
#include <scripting/builtins/imports.h>
#include <scripting/builtins/exports.h>
#include <scripting/builtins/environment.h>

namespace Framework::Integrations::Server::Scripting {

    ServerScriptingModule::ServerScriptingModule() {
        // Create Node.js engine without sandbox for full server capabilities
        Framework::Scripting::NodeEngineOptions options;
        options.sandboxed = false;
        options.processName = "mafiahub-server";
#ifdef FW_NODE_INSPECTOR
        options.enableInspector = true;
        options.inspectorPort = 9229;
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
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Failed to initialize Node.js engine: {}", _nodeEngine->GetLastError());
            return Framework::Scripting::ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
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
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(
            "JS Server scripting module initialized with Node.js engine");

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

        // Get or create Core global object
        v8::Local<v8::String> coreKey = v8::String::NewFromUtf8(isolate, "Core").ToLocalChecked();
        v8::Local<v8::Object> coreObj;
        v8::Local<v8::Value> existingCore;
        if (global->Get(context, coreKey).ToLocal(&existingCore) && existingCore->IsObject()) {
            coreObj = existingCore.As<v8::Object>();
        } else {
            coreObj = v8::Object::New(isolate);
            global->Set(context, coreKey, coreObj).Check();
        }

        // Register math type builtins on Core object
        Framework::Scripting::Builtins::RegisterAll(isolate, coreObj);

        // Register communication APIs
        _resourceManager->GetEvents().Register(isolate, context, coreObj, _resourceManager.get());
        Framework::Scripting::Messages::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::Imports::Register(isolate, context, frameworkObj, _resourceManager.get());
        Framework::Scripting::Exports::Register(isolate, context, frameworkObj, _resourceManager.get());

        // Register console override
        Framework::Scripting::Console::Register(isolate, context, _resourceManager.get());

        // Register environment info
        Framework::Scripting::Environment::Register(isolate, context, coreObj, false);

        // Register the chat API on the global object (Chat.sendToAll / Chat.sendToPlayer)
        Framework::Scripting::Builtins::Chat::Register(isolate, global);

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
        if (!result) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error(
                "Failed to start JS resources: {}", result.GetError());
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(
            "Started {} JS resource(s)", result.GetValue().size());
        return true;
    }

} // namespace Framework::Integrations::Server::Scripting
