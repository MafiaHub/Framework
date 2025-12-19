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

        std::vector<std::string> _clientFiles;
        std::vector<std::string> _serverFiles;

        std::string _mainGamemodePath;

      public:
        ServerScriptingModule(std::shared_ptr<World::ServerEngine>);
        ~ServerScriptingModule();

        bool Init(Framework::Scripting::SDKRegisterCallback);
        bool PreShutdown();
        bool Shutdown();
        bool LoadManifest();
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

        void SetMainGamemodePath(const std::string &path);
        std::string GetMainGamemodePath() const { return _mainGamemodePath; }

        std::vector<std::string> GetClientFiles() const {
            return _clientFiles;
        }

        std::vector<std::string> GetServerFiles() const {
            return _serverFiles;
        }

        /**
         * Get list of resources to send to clients.
         * Only includes resources with client_files defined.
         */
        std::vector<ClientResourceInfo> GetClientResourceList() const;
    };
} // namespace Framework::Integrations::Server::Scripting
