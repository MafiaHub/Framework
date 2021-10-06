/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <chrono>
#include <http/webserver.h>
#include <logging/logger.h>
#include <scripting/engine.h>
#include <string>

#include "networking/engine.h"

namespace Framework::Integrations::Server {
    struct InstanceOptions {
        std::string modSlug;
        std::string modName;
        std::string modVersion;
        std::string bindName;
        std::string bindHost;
        int32_t bindPort;
        std::string bindPassword;
        int32_t maxPlayers;
        std::string httpServeDir;
    };

    class Instance {
      private:
        bool _alive;
        std::chrono::time_point<std::chrono::high_resolution_clock> _nextTick;
        InstanceOptions _opts;

        Scripting::Engine *_scriptingEngine;
        Networking::Engine *_networkingEngine;
        HTTP::Webserver *_webServer;

      public:
        Instance();
        ~Instance();

        ServerError Init(InstanceOptions &);
        ServerError Shutdown();

        void InitEndpoints();

        void Update();

        void Run();

        bool IsAlive() const {
            return _alive;
        }

        Scripting::Engine *GetScriptingEngine() const {
            return _scriptingEngine;
        }

        Networking::Engine *GetNetworkingEngine() const {
            return _networkingEngine;
        }

        HTTP::Webserver *GetWebserver() const {
            return _webServer;
        }
    };
} // namespace Framework::Integrations::Server
