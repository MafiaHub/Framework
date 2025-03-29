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
        _luaEngine = new sol::state();

        // Make sure we have at least one server file to load
        if (_serverFiles.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("No server files to load");
            return EngineError::ENGINE_INIT_FAILED;
        }
        
        // Base setup for the lua state
        _luaEngine->set_exception_handler(&my_exception_handler);
        _luaEngine->open_libraries(sol::lib::base, sol::lib::table, sol::lib::package, sol::lib::coroutine, sol::lib::string, sol::lib::io, sol::lib::math, sol::lib::debug, sol::lib::os, sol::lib::utf8);

        // Configure the lua paths
        setLuaPath(_luaEngine->lua_state(), std::string(_mainGamemodeServerPath + "\\?.lua").c_str());
        setLuaPath(_luaEngine->lua_state(), std::string(_mainGamemodeServerPath + "\\?\\?.lua").c_str());

        // Init the common SDK
        InitCommonSDK();

        // Init the mod-level scripting layer
        if (cb) {
            cb(Framework::Scripting::SDKRegisterWrapper<Engine>(this));
        }

        // For now, always load the first in the list
        const std::string serverFile = _serverFiles[0];
        SetScriptName(serverFile);
        if (!LoadScript()) {
            return EngineError::ENGINE_INIT_FAILED;
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

        auto lr = _luaEngine->load_file(_mainGamemodeServerPath + "/" + _scriptName);
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

    EngineError ServerEngine::LoadManifest() {
        // Ensure main path exists
        cppfs::FileHandle mainFolder = cppfs::fs::open(_mainGamemodePath);
        if (!mainFolder.exists()) {
            if (!mainFolder.createDirectory()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to create main directory at {}", _mainGamemodePath);
                return EngineError::ENGINE_MANIFEST_LOADING_FAILED;
            }
        }

        // Check/create manifest.json
        cppfs::FileHandle manifestFile = cppfs::fs::open(_mainGamemodePath + "/manifest.json");
        if (!manifestFile.exists() || !manifestFile.isFile()) {
            // Create default manifest
            nlohmann::json defaultManifest;
            defaultManifest["client_files"] = std::vector<std::string>();
            defaultManifest["server_files"] = std::vector<std::string>();

            try {
                const std::string manifestContent = defaultManifest.dump(4);
                manifestFile.writeFile(manifestContent);
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Created default manifest.json");

                // Set empty arrays for initial state
                _clientFiles.clear();
                _serverFiles.clear();
                return EngineError::ENGINE_NONE;
            }
            catch (const std::exception &e) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to write manifest.json: {}", e.what());
                return EngineError::ENGINE_MANIFEST_LOADING_FAILED;
            }
        }

        // Load existing manifest
        try {
            std::string manifestJsonContent = manifestFile.readFile();
            if (manifestJsonContent.empty()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The gamemode manifest.json is empty");
                return EngineError::ENGINE_MANIFEST_LOADING_FAILED;
            }

            auto root    = nlohmann::json::parse(manifestJsonContent);
            _clientFiles = root["client_files"].get<std::vector<std::string>>();
            _serverFiles = root["server_files"].get<std::vector<std::string>>();
        }
        catch (nlohmann::detail::type_error &err) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The gamemode manifest.json is not valid:\n\t{}", err.what());
            return EngineError::ENGINE_MANIFEST_LOADING_FAILED;
        }
        catch (const std::exception &e) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to read manifest.json: {}", e.what());
            return EngineError::ENGINE_MANIFEST_LOADING_FAILED;
        }

        return EngineError::ENGINE_NONE;
    }
} // namespace Framework::Scripting
