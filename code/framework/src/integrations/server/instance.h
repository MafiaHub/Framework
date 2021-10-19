/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "external/firebase/wrapper.h"
#include "masterlist.h"
#include "networking/engine.h"
#include "utils/config.h"
#include "world/server.h"

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
        std::string modHelpText;
        std::string modName;
        std::string modVersion;
        std::string modConfigFile = "server.json";

        std::string bindName;
        std::string bindHost;
        std::string bindSecretKey;
        std::string bindMapName;
        int32_t bindPort;
        std::string bindPassword;
        bool bindPublicServer = false;

        int32_t maxPlayers;
        std::string httpServeDir;

        bool enableSignals;

        // update intervals
        uint32_t tickInterval = 3334;

        // args
        int argc;
        char **argv;

        // firebase
        bool firebaseEnabled = false;
        std::string firebaseProjectId;
        std::string firebaseAppId;
        std::string firebaseApiKey;
    };

    class Instance {
      private:
        bool _alive;
        std::chrono::time_point<std::chrono::high_resolution_clock> _nextTick;

        InstanceOptions _opts;

        std::unique_ptr<Scripting::Engine> _scriptingEngine;
        std::unique_ptr<Networking::Engine> _networkingEngine;
        std::unique_ptr<HTTP::Webserver> _webServer;
        std::unique_ptr<World::ServerEngine> _worldEngine;
        std::unique_ptr<External::Firebase::Wrapper> _firebaseWrapper;
        std::unique_ptr<Masterlist> _masterlistSync;
        std::unique_ptr<Utils::Config> _fileConfig;

        void InitEndpoints();
        void InitModules();
        void InitManagers();
        void InitMessages();
        bool LoadConfigFromJSON();

        // managers
        flecs::entity _weatherManager;

      public:
        Instance();
        ~Instance();

        ServerError Init(InstanceOptions &);
        ServerError Shutdown();

        virtual void PostInit() {}

        virtual void PostUpdate() {}

        virtual void PreShutdown() {}

        void Update();

        void Run();

        void OnSignal(const sig_signal_t);

        bool IsAlive() const {
            return _alive;
        }

        InstanceOptions &GetOpts() {
            return _opts;
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
