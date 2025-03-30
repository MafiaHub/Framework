/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>
#include <map>
#include <vector>
#include <string>

#include <cppfs/FileHandle.h>
#include <cppfs/FileWatcher.h>
#include <cppfs/fs.h>

#include <logging/logger.h>
#include <utils/time.h>

#include "engine.h"
namespace Framework::Scripting {
    class ServerEngine : public Engine {
      private:
        // Global
        cppfs::FileWatcher *_watcher;
        Utils::Time::TimePoint _nextFileWatchUpdate;
        int32_t _fileWatchUpdatePeriod = 1000;

        // Package
        std::vector<std::string> _scripts;
        std::string _gamemodeName;
        std::string _mainGamemodePath;
        std::string _mainGamemodeServerPath;
        std::atomic<bool> _packageLoaded = false;

        // Gamemode
        bool _shouldReloadWatcher               = false;
        
      public:
        EngineError Init(SDKRegisterCallback) override;
        EngineError Shutdown() override;
        void Update() override;

        bool LoadScript();
        bool UnloadScript();

        bool IsPackageLoaded() const {
            return _packageLoaded;
        }

        std::string GetGamemodeName() const {
            return _gamemodeName;
        }

        void SetGamemodeName(const std::string &name) {
            _gamemodeName = name;
        }

        std::string GetMainGamemodePath() const {
            return _mainGamemodePath;
        }

        void SetMainGamemodePath(const std::string &path) {
            _mainGamemodePath = path;
            _mainGamemodeServerPath = fmt::format("{}\\server", path);
        }

        std::string GetMainGamemodeServerPath() const {
            return _mainGamemodeServerPath;
        }

        std::vector<std::string> GetScripts() const {
            return _scripts;
        }

        void AddScript(std::string name) {
            _scripts.push_back(name);
        }

        void AddScripts(std::vector<std::string> names) {
            _scripts.insert(_scripts.end(), names.begin(), names.end());
        }
    };
} // namespace Framework::Scripting::Engines::Node
