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
#include <functional>
#include <chrono>
#include <memory>

#include <cppfs/FileWatcher.h>
#include <cppfs/fs.h>
#include <cppfs/FileHandle.h>

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

        // File watching
        cppfs::FileWatcher *_watcher = nullptr;
        std::chrono::time_point<std::chrono::high_resolution_clock> _nextFileWatchUpdate;
        int32_t _fileWatchUpdatePeriod = 1000;
        bool _shouldReloadWatcher = false;
        std::function<void()> _onReloadCallback;

      public:
        ServerScriptingModule(std::shared_ptr<World::ServerEngine>);
        ~ServerScriptingModule();

        bool Init(Framework::Scripting::SDKRegisterCallback);
        bool PreShutdown();
        bool Shutdown();
        bool LoadManifest();
        void Update();

        void SetOnReloadCallback(std::function<void()> callback) {
            _onReloadCallback = callback;
        }

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

        void ReloadScriptingEngine();

        /**
         * Get list of resources to send to clients.
         * Only includes resources with client_files defined.
         */
        std::vector<ClientResourceInfo> GetClientResourceList() const;

        /**
         * Send resource list to a specific client.
         * @param guid The client's GUID
         * @return True if message was sent successfully
         */
        bool SendResourceListToClient(SLNet::RakNetGUID guid);

        /**
         * Send resource list to all connected clients.
         * @return Number of clients the message was sent to
         */
        size_t SendResourceListToAllClients();

      private:
        void UpdateFileWatcher();
        void SetupWatchPath(const std::string &path);
    };
} // namespace Framework::Integrations::Server::Scripting
