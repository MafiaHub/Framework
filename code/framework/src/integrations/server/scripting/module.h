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

      public:
        ServerScriptingModule(std::shared_ptr<World::ServerEngine>);

        ~ServerScriptingModule() = default;

        bool Init(Framework::Scripting::SDKRegisterCallback);
        bool LoadManifest();

        std::shared_ptr<Framework::Scripting::ServerEngine> GetEngine() const {
            return _serverEngine;
        }

        std::shared_ptr<World::ServerEngine> GetWorldEngine() const {
            return _world;
        }

        void SetMainGamemodePath(const std::string &path) {
            _mainGamemodePath = path;
            if (_serverEngine != nullptr) {
                _serverEngine->SetMainGamemodePath(path);
            }
        }

        std::vector<std::string> GetClientFiles() const {
            return _clientFiles;
        }

        std::vector<std::string> GetServerFiles() const {
            return _serverFiles;
        }
    };
} // namespace Framework::Integrations::Scripting
