/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <nlohmann/json.hpp>

#include "core_modules.h"
#include "server_engine.h"
#include "types/events.h"

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

    // https://stackoverflow.com/questions/4125971/setting-the-global-lua-path-variable-from-c-c
    static inline int setLuaPath(lua_State *L, const char *path) {
        lua_getglobal(L, "package");
        lua_getfield(L, -1, "path");                // get field "path" from table at top of stack (-1)
        std::string cur_path = lua_tostring(L, -1); // grab path string from top of stack
        cur_path.append(";");                       // do your path magic here
        cur_path.append(path);
        lua_pop(L, 1);                       // get rid of the string on the stack we just pushed on line 5
        lua_pushstring(L, cur_path.c_str()); // push the new one
        lua_setfield(L, -2, "path");         // set the field "path" in table at -2 with value at top of stack
        lua_pop(L, 1);                       // get rid of package table from top of stack
        return 0;                            // all done!
    }

    EngineError ServerEngine::Init(SDKRegisterCallback cb) {
        // Base setup for the lua state
        _luaEngine.set_exception_handler(&my_exception_handler);
        _luaEngine.open_libraries(sol::lib::base, sol::lib::table, sol::lib::package, sol::lib::coroutine, sol::lib::string, sol::lib::io, sol::lib::math, sol::lib::debug, sol::lib::os, sol::lib::utf8);

        // Configure the lua paths
        setLuaPath(_luaEngine.lua_state(), std::string(_executionPath + "\\?.lua").c_str());
        setLuaPath(_luaEngine.lua_state(), std::string(_executionPath + "\\?\\?.lua").c_str());

        // Init the common SDK
        InitCommonSDK();

        // Init the mod-level scripting layer
        if (cb) {
            cb(Framework::Scripting::SDKRegisterWrapper<Engine>(this));
        }
        
        return EngineError::ENGINE_NONE;
    }

    EngineError ServerEngine::Shutdown() {
        UnloadScript();
        return EngineError::ENGINE_NONE;
    }

    void ServerEngine::Update() {
        if(!IsPackageLoaded()){
            return;
        }

        InvokeEvent(Events[EventIDs::GAMEMODE_UPDATED]);
    }

    bool ServerEngine::LoadScript() {
        if(IsPackageLoaded()){
            return false;
        }

        auto lr = _luaEngine.load_file(_executionPath + "/" + _scriptName);
        if (!lr.valid()) { // This checks the syntax of your script, but does not execute it
            sol::error err   = lr;
            std::string what = err.what();
            std::cout << "call failed, sol::error::what() is " << what << std::endl;
            return false;
        }
        
        sol::protected_function_result result1 = lr(); // this causes the script to execute
        if (!result1.valid()) {
            sol::error err   = result1;
            std::string what = err.what();
            std::cout << "call failed, sol::error::what() is " << what << std::endl;
            return false;
        }

        InvokeEvent(Events[EventIDs::GAMEMODE_LOADED]);
        _packageLoaded = true;
        return true;
    }

    bool ServerEngine::UnloadScript() {
        if(!IsPackageLoaded()){
            return false;
        }

        InvokeEvent(Events[EventIDs::GAMEMODE_UNLOADING]);
        _packageLoaded = false;
        return true;
    }
} // namespace Framework::Scripting
