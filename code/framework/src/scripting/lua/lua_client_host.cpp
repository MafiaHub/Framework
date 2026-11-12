/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_client_host.h"

#include "lua_runtime.h"

#include "../builtins/builtins.h"
#include "../resource/resource_manager.h"

#include <logging/logger.h>
#include <utils/profiler.h>

#include <utility>

namespace Framework::Scripting {
    LuaClientHost::LuaClientHost(LuaEngineOptions options): _engine(options) {
        // A value type or a handle crosses between a state and the isolate, and between two states, as itself. The
        // client has no other place that says so, and a project registers its own handle types after this.
        Builtins::RegisterTransferTypes();

        Builtins::WatchLuaRuntimes(_engine);

        // The manager is asked for when a state is created, which only the manager of that moment does.
        _engine.SetRuntimeSetupCallback([this](LuaRuntime &runtime) {
            Builtins::RegisterSharedLua(runtime, _engine.GetResourceManager(), _modVersion);
            for (const auto &setup : _setups) {
                setup(runtime);
            }
        });
    }

    void LuaClientHost::AddRuntimeSetup(std::function<void(LuaRuntime &)> setup) {
        _setups.push_back(std::move(setup));
    }

    ScriptingError LuaClientHost::Init(ResourceManager &manager, const std::string &modVersion) {
        _modVersion = modVersion;
        if (_engine.Init() != ScriptingError::SCRIPTING_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to initialize Lua engine: {}", _engine.GetLastError());
            return ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
        }
        manager.SetLuaEngine(&_engine);
        return ScriptingError::SCRIPTING_NONE;
    }

    void LuaClientHost::Update() {
        if (!_engine.IsInitialized()) {
            return;
        }
        FW_PROFILE_SCOPE_N("Scripting::Lua");
        _engine.Tick();

        // Queued, not handled where they were raised, so that a resource is stopped or restarted outside its script.
        ResourceManager *manager = _engine.GetResourceManager();
        for (const auto &error : _engine.DrainPendingErrors()) {
            if (manager) {
                manager->HandleResourceRuntimeError(error.resourceName, error.errorMessage);
            }
        }
    }

    void LuaClientHost::Shutdown() {
        _engine.Shutdown();
        // Of resources that are gone by now.
        _engine.DrainPendingErrors();
    }
} // namespace Framework::Scripting
