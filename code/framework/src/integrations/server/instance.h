/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include "errors.h"
#include "external/firebase/wrapper.h"
#include "http/webserver.h"
#include "logging/logger.h"
#include "networking/engine.h"
#include "scripting/server.h"
#include "services/masterlist.h"
#include "utils/config.h"
#include "world/server.h"

#include <flecs.h>

#include "world/types/player.hpp"
#include "world/types/streaming.hpp"

#include <chrono>
#include <memory>
#include <sig.h>
#include <string>
#include <utility>

namespace Framework::Integrations::Server {
    struct InstanceOptions {
        std::string modSlug;
        std::string modHelpText;
        std::string modName;
        std::string modVersion;
        std::string modConfigFile = "server.json";

        // networked game metadata (required)
        std::string gameName;
        std::string gameVersion;

        std::string bindHost;
        std::string bindSecretKey;
        std::string bindMapName;
        int32_t bindPort;
        std::string bindPassword;
        bool bindPublicServer = true;

        std::string webBindHost;
        int32_t webBindPort;

        int32_t maxPlayers;
        std::string httpServeDir;

        bool enableSignals;

        // update intervals
        float tickInterval         = 0.016667f;
        float streamerTickInterval = 0.033334f;

        // args
        int argc;
        char **argv;

        // firebase
        bool firebaseEnabled = false;
        std::string firebaseProjectId;
        std::string firebaseAppId;
        std::string firebaseApiKey;
    };

    using OnPlayerConnectionCallback = fu2::function<void(flecs::entity, uint64_t) const>;

    class Instance {
      private:
        bool _alive;
        bool _shuttingDown;
        std::chrono::time_point<std::chrono::high_resolution_clock> _nextTick;

        InstanceOptions _opts;

        std::shared_ptr<Scripting::ServerEngine> _scriptingEngine;
        std::shared_ptr<Networking::Engine> _networkingEngine;
        std::shared_ptr<HTTP::Webserver> _webServer;
        std::unique_ptr<External::Firebase::Wrapper> _firebaseWrapper;
        std::unique_ptr<Utils::Config> _fileConfig;
        std::shared_ptr<World::ServerEngine> _worldEngine;
        std::shared_ptr<Services::MasterlistConnector> _masterlist;

        void InitEndpoints();
        void InitModules() const;
        void InitNetworkingMessages() const;
        bool LoadConfigFromJSON();
        void RegisterScriptingBuiltins(Framework::Scripting::SDKRegisterWrapper);

        // managers
        flecs::entity _weatherManager;

        // entity factories
        std::shared_ptr<World::Archetypes::PlayerFactory> _playerFactory;
        std::shared_ptr<World::Archetypes::StreamingFactory> _streamingFactory;

        // callbacks
        OnPlayerConnectionCallback _onPlayerConnectCallback;
        OnPlayerConnectionCallback _onPlayerDisconnectCallback;

      public:
        Instance();
        ~Instance();

        ServerError Init(InstanceOptions &);
        ServerError Shutdown();

        virtual void PostInit() {}

        virtual void PostUpdate() {}

        virtual void PreShutdown() {}

        virtual void ModuleRegister(Framework::Scripting::SDKRegisterWrapper sdk) {
            (void)sdk;
        }

        void Update();

        void Run();

        void OnSignal(sig_signal_t);

        bool IsAlive() const {
            return _alive;
        }

        void SetOnPlayerConnectCallback(OnPlayerConnectionCallback onPlayerConnectCallback) {
            _onPlayerConnectCallback = std::move(onPlayerConnectCallback);
        }

        void SetOnPlayerDisconnectCallback(OnPlayerConnectionCallback onPlayerDisconnectCallback) {
            _onPlayerDisconnectCallback = std::move(onPlayerDisconnectCallback);
        }

        InstanceOptions &GetOpts() {
            return _opts;
        }

        std::shared_ptr<Scripting::ServerEngine> GetScriptingEngine() const {
            return _scriptingEngine;
        }

        std::shared_ptr<World::ServerEngine> GetWorldEngine() const {
            return _worldEngine;
        }

        std::shared_ptr<Networking::Engine> GetNetworkingEngine() const {
            return _networkingEngine;
        }

        std::shared_ptr<HTTP::Webserver> GetWebserver() const {
            return _webServer;
        }

        std::shared_ptr<World::Archetypes::PlayerFactory> GetPlayerFactory() const {
            return _playerFactory;
        }

        std::shared_ptr<World::Archetypes::StreamingFactory> GetStreamingFactory() const {
            return _streamingFactory;
        }
    };
} // namespace Framework::Integrations::Server
