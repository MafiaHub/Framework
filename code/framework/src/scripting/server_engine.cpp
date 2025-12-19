/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */


#include "core_modules.h"
#include "server_engine.h"
#include "types/events.h"

#include "builtins/resource.h"

#include <core_modules.h>
#include <world/engine.h>

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

    // Path normalization function for cross-platform Lua path handling
    static std::string normalizeLuaPath(const std::string &path) {
        std::string normalized = path;
        // Lua prefers forward slashes in package.path across all platforms
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        return normalized;
    }

    // https://stackoverflow.com/questions/4125971/setting-the-global-lua-path-variable-from-c-c
    static inline int setLuaPath(lua_State *L, const char *c_path) {
        const std::string path = c_path;
        lua_getglobal(L, "package");
        lua_getfield(L, -1, "path");                // get field "path" from table at top of stack (-1)
        std::string cur_path = lua_tostring(L, -1); // grab path string from top of stack
        cur_path.append(";");                       // do your path magic here
        cur_path.append(path + ".lua");
        lua_pop(L, 1);                       // get rid of the string on the stack we just pushed
        lua_pushstring(L, cur_path.c_str()); // push the new one
        lua_setfield(L, -2, "path");         // set the field "path" in table at -2 with value at top of stack
        lua_pop(L, 1);                       // get rid of package table from top of stack
        return 0;                            // all done!
    }
    static inline int setLuaCPath(lua_State *L, const char *c_path) {
        const std::string path = c_path;
        lua_getglobal(L, "package");
        lua_getfield(L, -1, "cpath");                // get field "cpath" from table at top of stack (-1)
        std::string cur_path = lua_tostring(L, -1); // grab path string from top of stack
        cur_path.append(";");                       // do your path magic here
        cur_path.append(path + ".dll");
        cur_path.append(";");
        cur_path.append(path + ".so");
        cur_path.append(";");
        cur_path.append(path + ".dylib");
        lua_pop(L, 1);                       // get rid of the string on the stack we just pushed
        lua_pushstring(L, cur_path.c_str()); // push the new one
        lua_setfield(L, -2, "cpath");         // set the field "cpath" in table at -2 with value at top of stack
        lua_pop(L, 1);                       // get rid of package table from top of stack
        return 0;                            // all done!
    }

    EngineError ServerEngine::Init(SDKRegisterCallback cb) {
        _luaEngine = new sol::state();
        
        // Base setup for the lua state
        _luaEngine->set_exception_handler(&my_exception_handler);
        _luaEngine->open_libraries(sol::lib::base, sol::lib::table, sol::lib::package, sol::lib::coroutine, sol::lib::string, sol::lib::io, sol::lib::math, sol::lib::debug, sol::lib::os, sol::lib::utf8);

        // Configure the lua paths using normalized paths for cross-platform compatibility
        setLuaPath(_luaEngine->lua_state(), normalizeLuaPath(_mainGamemodeServerPath + "/?").c_str());
        setLuaPath(_luaEngine->lua_state(), normalizeLuaPath(_mainGamemodeServerPath + "/?/?").c_str());
        setLuaPath(_luaEngine->lua_state(), normalizeLuaPath(_mainGamemodePath + "/?").c_str());
        setLuaPath(_luaEngine->lua_state(), normalizeLuaPath(_mainGamemodePath + "/?/?").c_str());
        setLuaPath(_luaEngine->lua_state(), normalizeLuaPath(_mainGamemodePath + "/../?").c_str());
        setLuaPath(_luaEngine->lua_state(), normalizeLuaPath(_mainGamemodePath + "/../?/?").c_str());
        setLuaCPath(_luaEngine->lua_state(), normalizeLuaPath(_mainGamemodeServerPath + "/lua_modules/lib/lua/5.4/?").c_str());
        setLuaCPath(_luaEngine->lua_state(), normalizeLuaPath(_mainGamemodeServerPath + "/lua_modules/lib/lua/5.4/?/?").c_str());

        // Init the common SDK
        InitCommonSDK();

        // Init the mod-level scripting layer
        if (cb) {
            cb(Framework::Scripting::SDKRegisterWrapper<Engine>(this));
        }

        // For now, always load the first in the list
        if (_scripts.size() > 0) {
            const std::string serverFile = _scripts[0];
            SetGamemodeName(serverFile);
            if (!LoadScript()) {
                return EngineError::ENGINE_INIT_FAILED;
            }
        }
        
        return EngineError::ENGINE_NONE;
    }

    EngineError ServerEngine::Shutdown() {
        UnloadScript();
        return EngineError::ENGINE_NONE;
    }

    void ServerEngine::Update() {
        if(!IsGamemodeLoaded()){
            return;
        }

        InvokeEvent(Events[EventIDs::GAMEMODE_UPDATED]);
    }

    bool ServerEngine::LoadScript() {
        if (IsGamemodeLoaded()) {
            return false;
        }

        auto lr = _luaEngine->load_file(normalizeLuaPath(_mainGamemodeServerPath + "/" + _gamemodeName).c_str());
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
        SetGamemodeLoaded(true);

        // Notify mod-level layer
        if (_onLoadProc) {
            _onLoadProc();
        }

        return true;
    }

    bool ServerEngine::UnloadScript() {
        if(!IsGamemodeLoaded()){
            return false;
        }

        // Notify mod-level layer
        if (_onUnloadProc) {
            _onUnloadProc();
        }

        // Destroy all server-spawned entities
        CoreModules::GetWorldEngine()->PurgeAllGameModeEntities();

        // Unload the script
        InvokeEvent(Events[EventIDs::GAMEMODE_UNLOADING]);
        SetGamemodeLoaded(false);
        return true;
    }
} // namespace Framework::Scripting
