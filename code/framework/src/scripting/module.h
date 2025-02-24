/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <map>
#include <vector>
#include <string>

#include "errors.h"
#include "shared.h"

#include "client_engine.h"
#include "server_engine.h"

namespace Framework::Scripting {
    class Module {
      private:
        std::string _mainPath;
        std::vector<std::string> _clientFiles;
        std::vector<std::string> _serverFiles;

        std::unique_ptr<ClientEngine> _clientEngine;
        std::unique_ptr<ServerEngine> _serverEngine;

      public:
        Module() = default;
        ~Module() = default;

        ModuleError InitClientEngine(SDKRegisterCallback);
        ModuleError InitServerEngine(SDKRegisterCallback);
        ModuleError Shutdown();

        ModuleError LoadManifest();

        void Update() const;
        
        ClientEngine *GetClientEngine() const {
            return _clientEngine.get();
        }

        ServerEngine *GetServerEngine() const {
            return _serverEngine.get();
        }

        std::vector<std::string> GetClientFiles() const {
            return _clientFiles;
        }

        std::vector<std::string> GetServerFiles() const {
            return _serverFiles;
        }

        const std::string GetMainPath() const {
            return _mainPath;
        }

        void SetMainPath(const std::string &mainPath) {
            _mainPath = mainPath;
        }
    };
} // namespace Framework::Scripting
