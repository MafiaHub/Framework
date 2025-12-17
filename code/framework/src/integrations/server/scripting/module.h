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
     * Phase 7: Supports sending resource list to clients and controlling
     * client resources remotely.
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

        // Resource synchronization (Phase 7)

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

        /**
         * Send a resource command to a specific client.
         * @param guid The client's GUID
         * @param commandType The command type (start/stop/restart/reload)
         * @param resourceName Name of the resource
         * @param version Version of the resource (for start command)
         * @param hash Content hash (for start command)
         * @return True if message was sent successfully
         */
        bool SendResourceCommandToClient(SLNet::RakNetGUID guid, uint8_t commandType, const std::string &resourceName, const std::string &version = "", uint32_t hash = 0);

        /**
         * Send a resource command to all connected clients.
         * @param commandType The command type (start/stop/restart/reload)
         * @param resourceName Name of the resource
         * @param version Version of the resource (for start command)
         * @param hash Content hash (for start command)
         * @return Number of clients the command was sent to
         */
        size_t SendResourceCommandToAllClients(uint8_t commandType, const std::string &resourceName, const std::string &version = "", uint32_t hash = 0);

      private:
        void UpdateFileWatcher();
        void SetupWatchPath(const std::string &path);
    };
} // namespace Framework::Integrations::Server::Scripting
