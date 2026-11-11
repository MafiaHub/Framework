/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "module.h"

#include <logging/logger.h>
#include <utils/profiler.h>

#include <scripting/builtins/builtins.h>
#include <scripting/builtins/chat.h>

#include "builtins/pending_connection.h"
#include "builtins/voice.h"
#include <scripting/builtins/console.h>
#include <scripting/builtins/events.h>
#include <scripting/builtins/execution_environment.h>
#include <scripting/builtins/exports.h>
#include <scripting/builtins/imports.h>
#include <scripting/builtins/messages.h>
#include <scripting/engine_scope.h>
#include <scripting/event_metadata.h>
#include <scripting/lua/lua_runtime.h>
#include <scripting/scripting_catalog.h>
#include <scripting/timer_metadata.h>

#include <algorithm>
#include <optional>
#include <thread>

namespace Framework::Integrations::Server::Scripting {

    namespace {
        // The least time between two Lua ticks while the server waits for its next tick. Node has the wait in
        // between, and NodeEngine::RunUntil starts no script in its last millisecond: a Lua timer due every
        // millisecond would otherwise leave Node nothing of the wait.
        constexpr std::chrono::milliseconds kLuaWaitGranularity {4};
    } // namespace

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

        // Lua scripts are sandboxed on the server too, with room for server-sized state and no limit on how long
        // an entry may run.
        Framework::Scripting::LuaEngineOptions luaOptions;
        luaOptions.side             = Framework::Scripting::Bind::Side::Server;
        luaOptions.memoryLimitBytes = 512u << 20;
        luaOptions.entryDeadline    = std::chrono::milliseconds(0);
        _luaEngine                  = std::make_unique<Framework::Scripting::LuaEngine>(luaOptions);

        // Before any project registers its own handle types (PostInit runs ahead of Init): the newest is tried first,
        // so the framework's bases must come first.
        Framework::Scripting::Builtins::RegisterTransferTypes();
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

        // The Lua engine is up before the resource manager can start a resource in it.
        _luaEngine->SetRuntimeSetupCallback([this](Framework::Scripting::LuaRuntime &runtime) {
            RegisterLuaBindings(runtime);
        });
        if (_luaEngine->Init() != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to initialize Lua engine: {}", _luaEngine->GetLastError());
            return Framework::Scripting::ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
        }

        // Initialize ResourceManager with server-side config
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath         = _resourcesPath;
        config.isClient              = false;
        config.cascadeStopDependents = true;
        config.devMode               = _devMode;

        _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(_nodeEngine.get(), config);
        _resourceManager->SetLuaEngine(_luaEngine.get());

        // Register Framework SDK bindings
        RegisterFrameworkBindings();

        // Initialize Framework SDK in the engine
        _nodeEngine->InitFrameworkSDK();

        // Every resource runs in a runtime of its own, which needs the same bindings as the engine's.
        // The engine runs this inside each new runtime, where GetIsolate() names it.
        _nodeEngine->SetRuntimeSetupCallback([this](Framework::Scripting::Engine *engine) {
            RegisterFrameworkBindings();
            engine->InitFrameworkSDK();
        });

        if (!v8pp::metadata::export_catalog_from_environment("framework-server", "FRAMEWORK_SCRIPTING_API_METADATA")) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to export Framework server scripting API metadata");
        }

        // Install uncaught exception handler to prevent async errors from
        // crashing the server process and route them to the resource system
        {
            Framework::Scripting::EngineScope scope(_nodeEngine.get());

            _nodeEngine->InstallUncaughtExceptionHandler(_resourcesPath);
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("JS Server scripting module initialized with Node.js engine");

        _initialized = true;
        return Framework::Scripting::ScriptingError::SCRIPTING_NONE;
    }

    void ServerScriptingModule::RegisterFrameworkBindings() {
        if (!_nodeEngine || !_nodeEngine->IsInitialized()) {
            return;
        }

        Framework::Scripting::EngineScope scope(_nodeEngine.get());
        v8::Isolate *isolate           = scope.Isolate();
        v8::Local<v8::Context> context = scope.Context();

        v8::Local<v8::Object> global = context->Global();
        Framework::Scripting::SetScriptingCatalog(isolate, "framework-server");
        Framework::Scripting::SetScriptingEnvironment(isolate, /*isClient*/ false);

        // The events the framework raises; a mod's catalog blends them into its own EventMap.
        Framework::Scripting::RegisterEventMetadata(Framework::Scripting::GetScriptingCatalog(isolate));
        Framework::Scripting::GetScriptingCatalog(isolate)
            .data_type("EventMap")
            .add_property("consoleCommand", "[command: string, args: string[]]",
                "Dispatched for a line typed into the server console that no built-in command (help, ensure, refresh, ...) claimed. `args` is the rest of the line, split on whitespace. The console is the operator's, so this is the "
                "place for commands no player may run.");

        // The engine installs the timers without a binding; declare them for it.
        Framework::Scripting::RegisterTimerMetadata(Framework::Scripting::GetScriptingCatalog(isolate), /*isClient*/ false);

        // Every builtin registers at the global root (new Vector3, not new Core.Vector3).
        Framework::Scripting::Builtins::RegisterValueTypes(isolate, global);

        _resourceManager->GetEvents().Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Messages::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Imports::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Exports::Register(isolate, context, global, _resourceManager.get());
        Framework::Scripting::Builtins::Console::Register(isolate, context, _resourceManager.get());
        Framework::Scripting::Builtins::ExecutionEnvironment::Register(isolate, context, global, false, _modVersion);
        Framework::Scripting::Builtins::Chat::Register(isolate, global);
        Builtins::Voice::Register(isolate, global);
        Builtins::PendingConnection::Register(isolate);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Registered Framework JS bindings");
    }

    void ServerScriptingModule::RegisterLuaBindings(Framework::Scripting::LuaRuntime &runtime) {
        // As in JavaScript, every builtin is a global (Vector3.new, not Core.Vector3.new).
        Framework::Scripting::Builtins::RegisterValueTypesLua(runtime.State(), Framework::Scripting::Bind::Side::Server);
    }

    bool ServerScriptingModule::PreShutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
        }
        return true;
    }

    void ServerScriptingModule::Shutdown() {
        _resourceManager.reset();

        // After the resources, whose Lua states it owns, and before the Node engine.
        if (_luaEngine) {
            _luaEngine->Shutdown();
            _luaEngine.reset();
        }

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
            HandleRuntimeErrors();
        }

        TickLua();

        if (_resourceManager) {
            FW_PROFILE_SCOPE_N("Scripting::ResourceUpkeep");
            // Process scheduled restarts
            _resourceManager->ProcessScheduledRestarts();
            // Dev-mode: poll resource files and hot-reload on change
            _resourceManager->ProcessFileWatch();
        }

        // Process pending message responses
        if (_nodeEngine && _nodeEngine->IsInitialized() && _resourceManager) {
            FW_PROFILE_SCOPE_N("Scripting::PendingResponses");
            Framework::Scripting::EngineScope scope(_nodeEngine.get());
            v8::Isolate *isolate           = scope.Isolate();
            v8::Local<v8::Context> context = scope.Context();

            Framework::Scripting::Builtins::Messages::ProcessPendingResponses(isolate, context);
        }
    }

    void ServerScriptingModule::WaitUntil(std::chrono::steady_clock::time_point deadline) {
        if (!_nodeEngine || !_nodeEngine->IsInitialized()) {
            std::this_thread::sleep_until(deadline);
            return;
        }
        // RunUntil comes back early with an error waiting, so the resource is stopped or restarted before it runs on.
        while (std::chrono::steady_clock::now() < deadline) {
            ++_waitRounds;

            // Node has the wait, up to when Lua next has a timer to fire or a coroutine to wake. Only what comes
            // due after the last Lua tick counts: what that tick left due, as a sleep or a timer of no delay is,
            // goes on at the next Update, once for each tick of the server. So does what is due at the deadline
            // or later.
            std::optional<std::chrono::steady_clock::time_point> luaWake;
            if (const auto due = _luaEngine->NextDue(_lastLuaTick)) {
                const auto wake = std::max(*due, _lastLuaTick + kLuaWaitGranularity);
                if (wake < deadline) {
                    luaWake = wake;
                }
            }

            _nodeEngine->RunUntil(luaWake.value_or(deadline));
            HandleRuntimeErrors();
            if (luaWake && std::chrono::steady_clock::now() >= *luaWake) {
                TickLua(_lastLuaTick);
            }
        }
    }

    void ServerScriptingModule::TickLua(std::chrono::steady_clock::time_point after) {
        if (!_luaEngine || !_luaEngine->IsInitialized()) {
            return;
        }
        FW_PROFILE_SCOPE_N("Scripting::Lua");
        const auto now = std::chrono::steady_clock::now();
        _luaEngine->Tick(now, after);
        _lastLuaTick = now;

        // Queued, not handled where they were raised, so that a resource is stopped or restarted outside its script.
        HandleRuntimeErrors();
    }

    void ServerScriptingModule::HandleRuntimeErrors() {
        for (const auto &err : _nodeEngine->DrainPendingErrors()) {
            if (_resourceManager) {
                _resourceManager->HandleResourceRuntimeError(err.resourceName, err.errorMessage);
            }
        }
        for (const auto &err : _luaEngine->DrainPendingErrors()) {
            if (_resourceManager) {
                _resourceManager->HandleResourceRuntimeError(err.resourceName, err.errorMessage);
            }
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
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("No resources discovered in: {}", _resourcesPath);
            return true; // Not an error, just no resources
        }

        // Start all discovered resources
        auto result = _resourceManager->StartAll();
        if (!result) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to start resources: {}", result.GetError());
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Started {} resource(s)", result.GetValue().size());
        return true;
    }

} // namespace Framework::Integrations::Server::Scripting
