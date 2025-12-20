/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "client_engine.h"

#include <logging/logger.h>

namespace Framework::Scripting {
    namespace {
        int exception_handler(lua_State *L, sol::optional<const std::exception &> maybeException, sol::string_view description) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Lua error: {}", description);

            if (maybeException) {
                const std::exception &ex = *maybeException;
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Exception: {}", ex.what());
            }

            return sol::stack::push(L, description);
        }
    } // namespace

    EngineError ClientEngine::Init(SDKRegisterCallback cb) {
        _luaEngine = new sol::state();

        _luaEngine->set_exception_handler(&exception_handler);

        // Open all standard libraries - per-resource sandboxing is handled by
        // ResourceManager using EnvironmentSandbox::SetupClientSandbox()
        _luaEngine->open_libraries(sol::lib::base, sol::lib::table, sol::lib::package, sol::lib::string, sol::lib::math, sol::lib::coroutine, sol::lib::utf8, sol::lib::os);

        // Register common SDK builtins
        InitCommonSDK();

        // Initialize mod-level scripting layer if callback provided
        if (cb) {
            cb(Framework::Scripting::SDKRegisterWrapper<Engine>(this));
        }

        return EngineError::ENGINE_NONE;
    }

    EngineError ClientEngine::Shutdown() {
        if (!_luaEngine) {
            return EngineError::ENGINE_NONE;
        }

        _shutdownInProgress = true;

        if (_onUnloadProc) {
            _onUnloadProc();
        }

        _eventHandlers.clear();

        delete _luaEngine;
        _luaEngine = nullptr;

        _shutdownInProgress = false;
        return EngineError::ENGINE_NONE;
    }

    void ClientEngine::Update() {
        // Resource updates are handled by the ResourceManager
    }
} // namespace Framework::Scripting
