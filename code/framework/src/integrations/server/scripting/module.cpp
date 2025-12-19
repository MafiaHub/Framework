/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "module.h"

#include <nlohmann/json.hpp>
#include <cppfs/fs.h>
#include <cppfs/FileHandle.h>
#include <logging/logger.h>

#include <scripting/resource/resource.h>

namespace Framework::Integrations::Server::Scripting {
    ServerScriptingModule::ServerScriptingModule(std::shared_ptr<World::ServerEngine> world): _world(world) {
        _serverEngine = std::make_shared<Framework::Scripting::ServerEngine>();
        CoreModules::SetScriptingEngine(_serverEngine.get());
    }

    ServerScriptingModule::~ServerScriptingModule() {
        if (_resourceManager) {
            _resourceManager->StopAll();
            _resourceManager.reset();
            CoreModules::SetResourceManager(nullptr);
        }
    }

    bool ServerScriptingModule::Init(Framework::Scripting::SDKRegisterCallback cb) {
        if (_serverEngine->Init(cb) != Framework::Scripting::EngineError::ENGINE_NONE) {
            _serverEngine.reset();
            return false;
        }

        // Initialize ResourceManager with server-side config
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = _mainGamemodePath.empty() ? "gamemode" : _mainGamemodePath;
        config.isClient = false;
        config.cascadeStopDependents = true;

        _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(_serverEngine->GetLuaEngine(), config);
        CoreModules::SetResourceManager(_resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Server scripting module initialized with ResourceManager");

        return true;
    }

    void ServerScriptingModule::SetMainGamemodePath(const std::string &path) {
        _mainGamemodePath = path;
        if (_serverEngine != nullptr) {
            _serverEngine->SetMainGamemodePath(path);
        }
    }

    void ServerScriptingModule::Update() {
        if (_serverEngine) {
            _serverEngine->Update();
        }
    }

    bool ServerScriptingModule::Shutdown() {
        if (_serverEngine) {
            _serverEngine->Shutdown();
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
                // Clear existing scripts and add the new ones
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

    bool ServerScriptingModule::PreShutdown() {
        _serverEngine->UnloadScript();
        _serverEngine->ClearScripts();
        _serverEngine->ClearEventHandlers();

        return true;
    }

    std::vector<ClientResourceInfo> ServerScriptingModule::GetClientResourceList() const {
        std::vector<ClientResourceInfo> result;

        if (!_resourceManager) {
            return result;
        }

        // Get all running resources that have client files
        auto resourceNames = _resourceManager->GetAllResourceNames();
        for (const auto &name : resourceNames) {
            const Framework::Scripting::Resource *resource = _resourceManager->GetResource(name);
            if (!resource) {
                continue;
            }

            const auto &manifest = resource->GetManifest();

            // Only include resources that have client files
            if (!manifest.clientFiles.empty()) {
                ClientResourceInfo info;
                info.name = manifest.name;
                info.version = manifest.version;
                info.hash = resource->GetContentHash();
                result.push_back(info);
            }
        }

        return result;
    }

} // namespace Framework::Integrations::Server::Scripting
