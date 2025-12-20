/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "module.h"

#include "core_modules.h"

#include <cppfs/FileHandle.h>
#include <cppfs/FilePath.h>
#include <cppfs/fs.h>
#include <logging/logger.h>
#include <scripting/resource/resource.h>

namespace Framework::Integrations::Client::Scripting {

    ClientScriptingModule::ClientScriptingModule(std::shared_ptr<World::ClientEngine> world): _world(world) {
        _clientEngine = std::make_shared<Framework::Scripting::ClientEngine>();
        CoreModules::SetScriptingEngine(_clientEngine.get());
    }

    ClientScriptingModule::~ClientScriptingModule() {
        Shutdown();
    }

    bool ClientScriptingModule::Init(Framework::Scripting::SDKRegisterCallback cb) {
        if (_clientEngine->Init(cb) != Framework::Scripting::EngineError::ENGINE_NONE) {
            _clientEngine.reset();
            return false;
        }

        // Initialize ResourceManager with client-side config
        Framework::Scripting::ResourceManagerConfig config;
        config.resourcesPath = _resourceCachePath.empty() ? "resources" : _resourceCachePath;
        config.isClient = true;
        config.cascadeStopDependents = true;

        _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(_clientEngine->GetLuaEngine(), config);

        // Register ResourceManager with CoreModules
        CoreModules::SetResourceManager(_resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Client scripting module initialized with ResourceManager");
        return true;
    }

    bool ClientScriptingModule::Shutdown() {
        if (_resourceManager) {
            // Stop all resources before shutdown
            _resourceManager->StopAll();
            _resourceManager.reset();
            CoreModules::SetResourceManager(nullptr);
        }

        if (_clientEngine) {
            _clientEngine->Shutdown();
        }

        _serverResourceList.clear();
        _resourcesSynced = false;

        return true;
    }

    void ClientScriptingModule::Update() {
        if (_clientEngine) {
            _clientEngine->Update();
        }

        if (_resourceManager) {
            // Process any scheduled restarts
            _resourceManager->ProcessScheduledRestarts();

            // Process message queue
            _resourceManager->ProcessMessageQueue();
        }
    }

    void ClientScriptingModule::SetResourceCachePath(const std::string &path) {
        _resourceCachePath = path;

        // Update ResourceManager config if it exists
        if (_resourceManager) {
            Framework::Scripting::ResourceManagerConfig config = _resourceManager->GetConfig();
            config.resourcesPath = path;
            _resourceManager->SetConfig(config);
        }
    }

    void ClientScriptingModule::OnServerResourceList(const std::vector<ServerResourceInfo> &resources) {
        _serverResourceList = resources;
        _resourcesSynced = false;

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Received resource list from server with {} resources", resources.size());

        // Check which resources need to be downloaded
        auto toDownload = GetResourcesToDownload();

        if (toDownload.empty()) {
            // All resources are available locally
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("All resources available locally");
            _resourcesSynced = true;

            if (_onResourceSyncComplete) {
                _onResourceSyncComplete(true);
            }
        }
        else {
            // Request downloads for missing resources
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("{} resources need to be downloaded", toDownload.size());

            for (const auto &resource : toDownload) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Requesting download: {} v{}", resource.name, resource.version);

                if (_onResourceDownloadNeeded) {
                    _onResourceDownloadNeeded(resource.name, resource.version, resource.hash);
                }
            }
        }
    }

    void ClientScriptingModule::OnResourceDownloaded(const std::string &resourceName) {
        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Resource downloaded: {}", resourceName);

        // Discover the downloaded resource
        DiscoverCachedResource(resourceName);

        // Check if all resources are now available
        auto toDownload = GetResourcesToDownload();

        if (toDownload.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("All resources downloaded and available");
            _resourcesSynced = true;

            if (_onResourceSyncComplete) {
                _onResourceSyncComplete(true);
            }
        }
    }

    void ClientScriptingModule::StopAllResources() {
        // Stop all running resources but keep the ResourceManager and engine alive.
        // This allows StartAllResources() to be called again without requiring
        // a full re-initialization via Init(). Use Shutdown() for full cleanup.
        if (_resourceManager) {
            _resourceManager->StopAll();
        }

        // Note: We intentionally do NOT clear _serverResourceList here
        // to preserve it for StartAllResources() after re-download
        _resourcesSynced = false;
    }

    bool ClientScriptingModule::StartAllResources() {
        if (!_resourceManager) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("ResourceManager not initialized");
            return false;
        }

        // Discover all resources from cache based on server list
        for (const auto &serverResource : _serverResourceList) {
            if (!_resourceManager->HasResource(serverResource.name)) {
                DiscoverCachedResource(serverResource.name);
            }
        }

        // Start all discovered resources
        auto result = _resourceManager->StartAll();
        if (!result.success) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to start all resources: {}", result.error);
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Started {} resources", result.affectedResources.size());
        return true;
    }

    // State queries

    bool ClientScriptingModule::IsResourceAvailable(const std::string &resourceName, const std::string &version, uint32_t hash) const {
        std::string resourcePath = GetResourcePath(resourceName);

        // Check if directory exists
        cppfs::FileHandle dir = cppfs::fs::open(resourcePath);
        if (!dir.exists() || !dir.isDirectory()) {
            return false;
        }

        // Check if manifest exists
        std::string manifestPath = resourcePath + "/manifest.json";
        cppfs::FileHandle manifest = cppfs::fs::open(manifestPath);
        if (!manifest.exists() || !manifest.isFile()) {
            return false;
        }

        // Validate hash if provided (non-zero hash means server wants validation)
        if (hash != 0) {
            // Create a temporary Resource to calculate the local hash
            Framework::Scripting::Resource tempResource(resourcePath);
            if (!tempResource.IsManifestValid()) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Resource '{}' manifest is invalid", resourceName);
                return false;
            }

            // Check version match
            if (!version.empty() && tempResource.GetVersion() != version) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Resource '{}' version mismatch: local={}, server={}", resourceName, tempResource.GetVersion(), version);
                return false;
            }

            // Check hash match
            uint32_t localHash = tempResource.GetContentHash();
            if (localHash != hash) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Resource '{}' hash mismatch: local={}, server={}", resourceName, localHash, hash);
                return false;
            }
        }

        return true;
    }

    std::string ClientScriptingModule::GetResourcePath(const std::string &resourceName) const {
        if (_resourceCachePath.empty()) {
            return resourceName;
        }
        cppfs::FilePath basePath(_resourceCachePath);
        return basePath.resolve(resourceName).fullPath();
    }

    std::vector<ServerResourceInfo> ClientScriptingModule::GetResourcesToDownload() const {
        std::vector<ServerResourceInfo> toDownload;

        for (const auto &resource : _serverResourceList) {
            if (!IsResourceAvailable(resource.name, resource.version, resource.hash)) {
                toDownload.push_back(resource);
            }
        }

        return toDownload;
    }

    bool ClientScriptingModule::DiscoverCachedResource(const std::string &resourceName) {
        if (!_resourceManager) {
            return false;
        }

        std::string resourcePath = GetResourcePath(resourceName);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Discovering cached resource: {} at {}", resourceName, resourcePath);

        return _resourceManager->DiscoverResource(resourcePath);
    }

} // namespace Framework::Integrations::Client::Scripting
