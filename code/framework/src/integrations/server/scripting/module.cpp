/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "module.h"

#include <nlohmann/json.hpp>

namespace Framework::Integrations::Server::Scripting {
    ServerScriptingModule::ServerScriptingModule(std::shared_ptr<World::ServerEngine> world): _world(world) {
        _serverEngine = std::make_unique<Framework::Scripting::ServerEngine>();
        CoreModules::SetScriptingEngine(_serverEngine.get());
    };

    bool ServerScriptingModule::Init(Framework::Scripting::SDKRegisterCallback cb) {
        if (_serverEngine->Init(cb) != Framework::Scripting::EngineError::ENGINE_NONE) {
            _serverEngine.reset();
            return false;
        }
        return true;
    }

    bool ServerScriptingModule::LoadManifest() {
        // Ensure main path exists
        cppfs::FileHandle mainFolder = cppfs::fs::open(_mainGamemodePath);
        if (!mainFolder.exists()) {
            if (!mainFolder.createDirectory()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to create main directory at {}", _mainGamemodePath);
                return false;
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
                return true;
            }
            catch (const std::exception &e) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to write manifest.json: {}", e.what());
                return false;
            }
        }

        // Load existing manifest
        try {
            std::string manifestJsonContent = manifestFile.readFile();
            if (manifestJsonContent.empty()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The gamemode manifest.json is empty");
                return false;
            }

            auto root    = nlohmann::json::parse(manifestJsonContent);
            _clientFiles = root["client_files"].get<std::vector<std::string>>();
            _serverFiles = root["server_files"].get<std::vector<std::string>>();
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Found {} client file(s) and {} server file(s)", _clientFiles.size(), _serverFiles.size());
            
            // Add the scripts to the lua engine
            if (_serverEngine != nullptr) {
                _serverEngine->AddScripts(_serverFiles);
            }
        }
        catch (nlohmann::detail::type_error &err) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The gamemode manifest.json is not valid:\n\t{}", err.what());
            return false;
        }
        catch (const std::exception &e) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to read manifest.json: {}", e.what());
            return false;
        }

        return true;
    }
}
