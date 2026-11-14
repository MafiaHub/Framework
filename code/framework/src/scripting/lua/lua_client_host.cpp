/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_client_host.h"

#include "lua_builtins.h"
#include "lua_runtime.h"

#include "../bind/call.h"
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
            // As the isolates are told through the console. Forgotten when the runtime goes (WatchLuaRuntimes), and
            // when the manager does.
            Bind::SetCallResourceManager(&runtime, _engine.GetResourceManager());
            _shared.Apply(runtime);
            // Each one called through a copy: a setup may add another, which moves the ones in the list. The one
            // it adds runs in this state too.
            for (size_t i = 0; i < _setups.size(); ++i) {
                const auto setup = _setups[i];
                setup(runtime);
            }
        });
    }

    void LuaClientHost::AddRuntimeSetup(std::function<void(LuaRuntime &)> setup) {
        _setups.push_back(std::move(setup));
    }

    ScriptingError LuaClientHost::Init(ResourceManager &manager, const std::string &modVersion) {
        Bind::Registrar registrar;
        Builtins::RegisterShared(registrar, Bind::Languages::Lua, manager, modVersion);
        _shared = registrar.Lua();
        if (_engine.Init() != ScriptingError::SCRIPTING_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to initialize Lua engine: {}", _engine.GetLastError());
            return ScriptingError::SCRIPTING_ENGINE_INIT_FAILED;
        }
        manager.SetLuaEngine(&_engine);
        return ScriptingError::SCRIPTING_NONE;
    }

    void LuaClientHost::Update() {
        FW_PROFILE_SCOPE_N("Scripting::Lua");
        // Also between Shutdown and Init: the engine then only closes the states a shutdown from inside a script
        // left open, which must not wait for the next connection and its new V8 context.
        _engine.Tick();
        if (!_engine.IsInitialized()) {
            return;
        }

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
