/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "module.h"

#include <nlohmann/json.hpp>
#include <logging/logger.h>

#include "integrations/shared/rpc/reload_assets.h"

namespace Framework::Integrations::Server::Scripting {
    ServerScriptingModule::ServerScriptingModule(std::shared_ptr<World::ServerEngine> world): _world(world), _watcher(nullptr) {
        _serverEngine = std::make_shared<Framework::Scripting::ServerEngine>();
        CoreModules::SetScriptingEngine(_serverEngine.get());
    }

    ServerScriptingModule::~ServerScriptingModule() {
        if (_watcher) {
            delete _watcher;
            _watcher = nullptr;
        }
    }

    bool ServerScriptingModule::Init(Framework::Scripting::SDKRegisterCallback cb) {
        if (_serverEngine->Init(cb) != Framework::Scripting::EngineError::ENGINE_NONE) {
            _serverEngine.reset();
            return false;
        }

        // Initialize file watcher
        try {
            _watcher = new cppfs::FileWatcher();
            
            // Register event handler
            _watcher->addHandler([this](cppfs::FileHandle &fh, cppfs::FileEvent event) {
                // Log event for debugging
                std::string type = (fh.isDirectory() ? "directory" : "file");
                std::string operation = ((event & cppfs::FileEvent::FileCreated) ? "created" :
                                       ((event & cppfs::FileEvent::FileRemoved) ? "removed" :
                                       ((event & cppfs::FileEvent::FileAttrChanged) ? "attributes changed" :
                                       "modified")));
                
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("File watch event: {} '{}' was {}", type, fh.path(), operation);
                
                // Mark for reload
                _shouldReloadWatcher = true;
            });
            
            // Set up for first update
            _nextFileWatchUpdate = std::chrono::high_resolution_clock::now();
            _fileWatchUpdatePeriod = 1000;
            
            // If we already have a path, set up watching
            if (!_mainGamemodePath.empty()) {
                SetupWatchPath(_mainGamemodePath);
            }
            
            return true;
        }
        catch (const std::exception &e) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to initialize file watcher: {}", e.what());
            if (_watcher) {
                delete _watcher;
                _watcher = nullptr;
            }
            return false;
        }
    }

    void ServerScriptingModule::SetMainGamemodePath(const std::string &path) {
        _mainGamemodePath = path;
        if (_serverEngine != nullptr) {
            _serverEngine->SetMainGamemodePath(path);
        }

        // Set up file watching for the gamemode path
        if (_watcher) {
            SetupWatchPath(path);
        }
    }
    
    void ServerScriptingModule::SetupWatchPath(const std::string &path) {
        if (!_watcher) {
            return;
        }
        
        try {
            // Open directory
            cppfs::FileHandle dir = cppfs::fs::open(path);
            if (dir.isDirectory()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Setting up file watching for '{}'", path);
                
                // Add directory to watcher with events and recursive mode
                _watcher->add(dir, 
                            cppfs::FileEvent::FileCreated | 
                            cppfs::FileEvent::FileRemoved | 
                            cppfs::FileEvent::FileModified | 
                            cppfs::FileEvent::FileAttrChanged, 
                            cppfs::RecursiveMode::Recursive);
            }
            else {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("'{}' is not a valid directory for file watching", path);
            }
        }
        catch (const std::exception &e) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to setup file watching for '{}': {}", path, e.what());
        }
    }

    void ServerScriptingModule::Update() {
        if (_serverEngine) {
            _serverEngine->Update();
        }

        if (_watcher) {
            UpdateFileWatcher();

            if (_shouldReloadWatcher) {
                ReloadScriptingEngine();
                _shouldReloadWatcher = false;
            }
        }
    }

    bool ServerScriptingModule::Shutdown() {
        if (_serverEngine) {
            _serverEngine->Shutdown();
        }
        
        if (_watcher) {
            delete _watcher;
            _watcher = nullptr;
        }
        
        return true;
    }

    void ServerScriptingModule::UpdateFileWatcher() {
        const auto now = std::chrono::high_resolution_clock::now();
        if (now >= _nextFileWatchUpdate) {
            // Use a timeout to prevent blocking
            _watcher->watch(100); // Poll with 100ms timeout
            _nextFileWatchUpdate = now + std::chrono::milliseconds(_fileWatchUpdatePeriod);
        }
    }

    void ServerScriptingModule::ReloadScriptingEngine() {
        if (!_serverEngine) {
            return;
        }

        // Unload and clear the state, then reload
        _serverEngine->UnloadScript();
        _serverEngine->ClearScripts();
        _serverEngine->ClearEventHandlers();
        if (!LoadManifest()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to load manifest.");
            return;
        }
        _serverEngine->LoadScript();

        // Notify all connected peers they should re-download assets
        const auto net = CoreModules::GetNetworkPeer();
        if (net) {
            Shared::RPC::ReloadAssets reloadAssets {};
            net->SendRPC(reloadAssets, SLNet::UNASSIGNED_RAKNET_GUID);
        }

        // Notify callback if set
        if (_onReloadCallback) {
            _onReloadCallback();
        }
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
}
