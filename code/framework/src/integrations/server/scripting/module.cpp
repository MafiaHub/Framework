/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "module.h"

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
        config.resourcesPath = _resourcesPath.empty() ? "resources" : _resourcesPath;
        config.isClient = false;
        config.cascadeStopDependents = true;

        _resourceManager = std::make_unique<Framework::Scripting::ResourceManager>(_serverEngine->GetLuaEngine(), config);
        CoreModules::SetResourceManager(_resourceManager.get());

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Server scripting module initialized with ResourceManager");

        return true;
    }

    void ServerScriptingModule::SetResourcesPath(const std::string &path) {
        _resourcesPath = path;
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

    bool ServerScriptingModule::PreShutdown() {
        if (_resourceManager) {
            _resourceManager->StopAll();
        }

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

    bool ServerScriptingModule::StartAllResources() {
        if (!_resourceManager) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("ResourceManager not initialized");
            return false;
        }

        // Discover all resources in the resources path
        size_t discovered = _resourceManager->DiscoverResources();
        if (discovered == 0) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->warn("No resources discovered in: {}", _resourcesPath);
            return true; // Not an error, just no resources
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Discovered {} resource(s)", discovered);

        // Start all discovered resources
        auto result = _resourceManager->StartAll();
        if (!result.success) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("Failed to start resources: {}", result.error);
            return false;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info("Started {} resource(s)", result.affectedResources.size());
        return true;
    }

} // namespace Framework::Integrations::Server::Scripting
