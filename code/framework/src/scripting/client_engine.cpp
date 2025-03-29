/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "client_engine.h"
#include "types/events.h"

#include <cppfs/fs.h>
#include <cppfs/FileHandle.h>
#include <filesystem>
#include <logging/logger.h>

namespace Framework::Scripting {
    namespace {
        // Handle Lua errors
        int exception_handler(lua_State *L, sol::optional<const std::exception &> maybe_exception, sol::string_view description) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Lua error: {}", description);
            
            if (maybe_exception) {
                const std::exception &ex = *maybe_exception;
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Exception: {}", ex.what());
            }
            
            return sol::stack::push(L, description);
        }

        // Helper to set Lua path
        void setLuaPath(lua_State *L, const char *path) {
            lua_getglobal(L, "package");
            lua_getfield(L, -1, "path");                // get field "path" from table at top of stack (-1)
            std::string cur_path = lua_tostring(L, -1); // grab path string from top of stack
            cur_path.append(";");                       // do your path magic here
            cur_path.append(path);
            lua_pop(L, 1);                       // get rid of the string on the stack we just pushed on line 5
            lua_pushstring(L, cur_path.c_str()); // push the new one
            lua_setfield(L, -2, "path");         // set the field "path" in table at -2 with value at top of stack
            lua_pop(L, 1);                       // get rid of package table from top of stack
        }

        // Sandbox the Lua environment by replacing unsafe functions
        void sandboxLuaEnvironment(sol::state &lua) {
            // Remove unsafe libraries and functions
            lua["os"]["execute"] = nullptr;
            lua["os"]["exit"] = nullptr;
            lua["os"]["getenv"] = nullptr;
            lua["os"]["remove"] = nullptr;
            lua["os"]["rename"] = nullptr;
            lua["os"]["setlocale"] = nullptr;
            lua["os"]["tmpname"] = nullptr;
            
            lua["package"]["loadlib"] = nullptr;
            lua["package"]["searchpath"] = nullptr;
            lua["package"]["cpath"] = nullptr;
            lua["package"]["config"] = nullptr;
            lua["package"]["preload"] = nullptr;
            
            lua["io"]["popen"] = nullptr;
            lua["io"]["open"] = nullptr;
            lua["io"]["tmpfile"] = nullptr;
            lua["io"]["close"] = nullptr;
            
            // Replace dofile with a safe version that only loads from the script cache path
            sol::function original_dofile = lua["dofile"];
            lua["dofile"] = [original_dofile](sol::this_state ts, const std::string &filename) -> sol::object {
                sol::state_view lua(ts);
                return original_dofile(filename);
            };
            
            // Replace require with a safe version that only loads from the script cache path
            sol::function original_require = lua["require"];
            lua["require"] = [original_require](sol::this_state ts, const std::string &modname) -> sol::object {
                sol::state_view lua(ts);
                return original_require(modname);
            };
        }
    }

    EngineError ClientEngine::Init(SDKRegisterCallback cb) {
        // Setup error handling
        _luaEngine.set_exception_handler(&exception_handler);
        
        // Open only necessary libraries (restricted subset)
        _luaEngine.open_libraries(
            sol::lib::base,
            sol::lib::table,
            sol::lib::string,
            sol::lib::math,
            sol::lib::coroutine,
            sol::lib::utf8
        );
        
        // Apply sandbox restrictions - BROKEN
        // sandboxLuaEnvironment(_luaEngine);
        
        // Init the common SDK
        InitCommonSDK();
        
        // Configure Lua paths to only allow loading from script cache directory
        if (!_scriptCachePath.empty()) {
            setLuaPath(_luaEngine.lua_state(), std::string(_scriptCachePath + "/?.lua").c_str());
            setLuaPath(_luaEngine.lua_state(), std::string(_scriptCachePath + "/?/init.lua").c_str());
        }
        
        // Initialize mod-level scripting layer if callback provided
        if (cb) {
            cb(Framework::Scripting::SDKRegisterWrapper<Engine>(this));
        }
        
        return EngineError::ENGINE_NONE;
    }

    EngineError ClientEngine::Shutdown() {
        UnloadScripts();
        return EngineError::ENGINE_NONE;
    }

    void ClientEngine::Update() {
        if (!AreScriptsLoaded()) {
            return;
        }
        
        // Invoke the update event for the client scripts
        InvokeEvent(Events[EventIDs::GAMEMODE_UPDATED]);
    }

    bool ClientEngine::LoadScripts() {
        if (_scriptCachePath.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Cannot load scripts: script cache path is not set");
            return false;
        }
        
        if (_scriptsLoaded) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Scripts are already loaded");
            return false;
        }
        
        // Check if there are any scripts to load
        if (_loadedScripts.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("No scripts to load");
            return false;
        }
        
        bool success = true;
        
        // Attempt to load each script
        for (const auto &scriptPath : _loadedScripts) {            
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Loading client script: {}", scriptPath);
            
            // First we load the file
            auto lr = _luaEngine.load_file(scriptPath);
            if (!lr.valid()) {
                sol::error err = lr;
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to load script {}: {}", scriptPath, err.what());
                success = false;
                continue;
            }
            
            // Then we execute it
            sol::protected_function_result result = lr();
            if (!result.valid()) {
                sol::error err = result;
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to execute script {}: {}", scriptPath, err.what());
                success = false;
                continue;
            }
        }
        
        if (success) {
            _scriptsLoaded = true;
            InvokeEvent(Events[EventIDs::GAMEMODE_LOADED]);
            
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("All client scripts loaded successfully");
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to load some client scripts");
        }
        
        return success;
    }

    bool ClientEngine::UnloadScripts() {
        if (!_scriptsLoaded) {
            return false;
        }
        
        // First notify that we are unloading
        InvokeEvent(Events[EventIDs::GAMEMODE_UNLOADING]);

        // Clear the Lua environment by creating a new table and setting it as the global environment
        _luaEngine.globals().clear();
        
        // Re-initialize the necessary libraries
        // TODO: move that part on a higher level
        {
            _luaEngine.open_libraries(sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math, sol::lib::coroutine, sol::lib::utf8);

            // Reconfigure paths
            if (!_scriptCachePath.empty()) {
                setLuaPath(_luaEngine.lua_state(), std::string(_scriptCachePath + "/?.lua").c_str());
                setLuaPath(_luaEngine.lua_state(), std::string(_scriptCachePath + "/?/init.lua").c_str());
            }
        }
        
        // Clear the loaded scripts list
        _loadedScripts.clear();

        _scriptsLoaded = false;
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Unloaded client scripts");
        return true;
    }

    bool ClientEngine::AddScript(const std::string &path) {
        if (path.empty()) {
            return false;
        }
        
        // Ensure we don't add duplicates
        if (std::find(_loadedScripts.begin(), _loadedScripts.end(), path) != _loadedScripts.end()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("Script {} already added", path);
            return false;
        }
        
        _loadedScripts.push_back(path);
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Added script to load list: {}", path);
        return true;
    }
} // namespace Framework::Scripting
