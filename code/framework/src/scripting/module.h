/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <map>
#include <string>

#include "errors.h"
#include "shared.h"

#include "client_engine.h"
#include "server_engine.h"

namespace Framework::Scripting {
    class Module {
      private:
        int _processArgsCount = 0;
        char **_processArgs   = nullptr;
        std::string _modName;

        std::unique_ptr<ClientEngine> _clientEngine;
        std::unique_ptr<ServerEngine> _serverEngine;

      public:
        Module()  = default;
        ~Module() = default;

        ModuleError InitClientEngine(SDKRegisterCallback);
        ModuleError InitServerEngine(SDKRegisterCallback);
        ModuleError Shutdown();

        void Update() const;
        
        ClientEngine *GetClientEngine() const {
            return _clientEngine.get();
        }

        ServerEngine *GetServerEngine() const {
            return _serverEngine.get();
        }

        void SetProcessArguments(int argc, char **argv) {
            _processArgsCount = argc;
            _processArgs      = argv;
        }

        void SetModName(std::string name) {
            _modName = name;
        }
    };
} // namespace Framework::Scripting
