/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "server_engine.h"

#include "builtins/resource.h"

namespace Framework::Scripting {
    int my_exception_handler(lua_State *L, sol::optional<const std::exception &> maybe_exception, sol::string_view description) {
        std::cout << "Lua error: ";
        if (maybe_exception) {
            std::cout << "(straight from the exception): ";
            const std::exception &ex = *maybe_exception;
            std::cout << ex.what() << std::endl;
        }
        else {
            std::cout << "(from the description parameter): ";
            std::cout.write(description.data(), static_cast<std::streamsize>(description.size()));
            std::cout << std::endl;
        }

        return sol::stack::push(L, description);
    }

    EngineError ServerEngine::Init(SDKRegisterCallback cb) {
        _luaEngine = new sol::state();

        // Base setup for the lua state
        _luaEngine->set_exception_handler(&my_exception_handler);
        _luaEngine->open_libraries(sol::lib::base, sol::lib::table, sol::lib::package, sol::lib::coroutine, sol::lib::string, sol::lib::io, sol::lib::math, sol::lib::debug, sol::lib::os, sol::lib::utf8);

        // Init the common SDK
        InitCommonSDK();

        // Init the mod-level scripting layer
        if (cb) {
            cb(Framework::Scripting::SDKRegisterWrapper<Engine>(this));
        }

        return EngineError::ENGINE_NONE;
    }

    EngineError ServerEngine::Shutdown() {
        return EngineError::ENGINE_NONE;
    }

    void ServerEngine::Update() {
        // Resource updates are handled by the ResourceManager
    }
} // namespace Framework::Scripting
