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

#include <cppfs/FileWatcher.h>
#include <cppfs/fs.h>
#include <cppfs/FileHandle.h>

#include <scripting/server_engine.h>
#include <world/server.h>

namespace Framework::Integrations::Server::Scripting {
    class ServerScriptingModule {
      private:
        std::shared_ptr<Framework::Scripting::ServerEngine> _serverEngine;
        std::shared_ptr<World::ServerEngine> _world;

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

        void SetMainGamemodePath(const std::string &path);
        std::string GetMainGamemodePath() const { return _mainGamemodePath; }

        std::vector<std::string> GetClientFiles() const {
            return _clientFiles;
        }

        std::vector<std::string> GetServerFiles() const {
            return _serverFiles;
        }

      private:
        void UpdateFileWatcher();
        void ReloadScriptingEngine();
        void SetupWatchPath(const std::string &path);
    };
} // namespace Framework::Integrations::Server::Scripting
