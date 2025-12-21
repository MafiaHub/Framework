/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <vector>
#include <string>
#include <memory>

#include <scripting/server_engine.h>
#include <scripting/resource/resource_manager.h>
#include <world/server.h>

#include <RakNetTypes.h>

namespace Framework::Integrations::Server::Scripting {

    /**
     * Information about a resource for sending to clients.
     */
    struct ClientResourceInfo {
        std::string name;
        std::string version;
        uint32_t hash = 0;
    };

    /**
     * Server-side scripting module with resource management support.
     *
     * Supports sending resource list to clients at connection time.
     * Clients run standalone after initial resource sync.
     */
    class ServerScriptingModule {
      private:
        std::shared_ptr<Framework::Scripting::ServerEngine> _serverEngine;
        std::shared_ptr<World::ServerEngine> _world;
        std::unique_ptr<Framework::Scripting::ResourceManager> _resourceManager;

        std::string _resourcesPath;

      public:
        ServerScriptingModule(std::shared_ptr<World::ServerEngine>);
        ~ServerScriptingModule();

        bool Init(Framework::Scripting::SDKRegisterCallback);
        bool PreShutdown();
        bool Shutdown();
        void Update();

        std::shared_ptr<Framework::Scripting::ServerEngine> GetEngine() const {
            return _serverEngine;
        }

        std::shared_ptr<World::ServerEngine> GetWorldEngine() const {
            return _world;
        }

        Framework::Scripting::ResourceManager *GetResourceManager() const {
            return _resourceManager.get();
        }

        void SetResourcesPath(const std::string &path);
        std::string GetResourcesPath() const { return _resourcesPath; }

        /**
         * Get list of resources to send to clients.
         * Only includes resources with client_files defined.
         */
        std::vector<ClientResourceInfo> GetClientResourceList() const;

        /**
         * Discover and start all resources using ResourceManager.
         */
        bool StartAllResources();
    };
} // namespace Framework::Integrations::Server::Scripting
