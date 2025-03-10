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
        std::string _scriptName;
        std::string _executionPath;
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

        std::string GetScriptName() const {
            return _scriptName;
        }

        void SetScriptName(const std::string &name) {
            _scriptName = name;
        }

        void SetExecutionPath(const std::string &path) {
            _executionPath = path;
        }
    };
} // namespace Framework::Scripting::Engines::Node
