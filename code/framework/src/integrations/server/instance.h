/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "networking/engine.h"
#include "world/engine.h"

#include <chrono>
#include <http/webserver.h>
#include <logging/logger.h>
#include <memory>
#include <scripting/engine.h>
#include <sig.h>
#include <string>

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
        bool enableSignals;
        uint32_t tickInterval = 3334;

        int argc;
        char **argv;
    };

    class Instance {
      private:
        bool _alive;
        std::chrono::time_point<std::chrono::high_resolution_clock> _nextTick;

        InstanceOptions _opts;

        std::unique_ptr<Scripting::Engine> _scriptingEngine;
        std::unique_ptr<Networking::Engine> _networkingEngine;
        std::unique_ptr<HTTP::Webserver> _webServer;
        std::unique_ptr<World::Engine> _worldEngine;

      public:
        Instance();
        ~Instance();

        ServerError Init(InstanceOptions &);
        ServerError Shutdown();

        void InitEndpoints();

        void Update();

        void Run();

        void OnSignal(const sig_signal_t);

        bool IsAlive() const {
            return _alive;
        }

        Scripting::Engine *GetScriptingEngine() const {
            return _scriptingEngine.get();
        }

        Networking::Engine *GetNetworkingEngine() const {
            return _networkingEngine.get();
        }

        HTTP::Webserver *GetWebserver() const {
            return _webServer.get();
        }
    };
} // namespace Framework::Integrations::Server
