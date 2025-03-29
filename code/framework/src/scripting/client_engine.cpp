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
    }

    EngineError ClientEngine::Init(SDKRegisterCallback cb) {
        _luaEngine = new sol::state();

        // Setup error handling
        _luaEngine->set_exception_handler(&exception_handler);
        
        // Open only necessary libraries (restricted subset)
        _luaEngine->open_libraries(
            sol::lib::base,
            sol::lib::table,
            sol::lib::string,
            sol::lib::math,
            sol::lib::coroutine,
            sol::lib::utf8
        );
        
        // Init the common SDK
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

        // First notify that we are unloading
        InvokeEvent(Events[EventIDs::GAMEMODE_UNLOADING]);

        // Clear the registered events
        _eventHandlers.clear();

        // Actually unload the state
        delete _luaEngine;
        _luaEngine = nullptr;

        _shutdownInProgress = false;
        return EngineError::ENGINE_NONE;
    }

    void ClientEngine::Update() {
        if (_shutdownInProgress || !_luaEngine) {
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
            auto lr = _luaEngine->load_file(scriptPath);
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
            InvokeEvent(Events[EventIDs::GAMEMODE_LOADED]);
            
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("All client scripts loaded successfully");
        } else {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to load some client scripts");
        }
        
        return success;
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
