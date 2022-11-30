/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../shared/types/player.hpp"
#include "../shared/types/streaming.hpp"
#include "errors.h"
#include "external/firebase/wrapper.h"
#include "http/webserver.h"
#include "logging/logger.h"
#include "networking/engine.h"
#include "scripting/server.h"
#include "scripting/engines/callback.h"
#include "utils/config.h"
#include "world/server.h"

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

        std::string bindName;
        std::string bindHost;
        std::string bindSecretKey;
        std::string bindMapName;
        int32_t bindPort;
        std::string bindPassword;
        bool bindPublicServer = false;

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

        // scripting
        Framework::Scripting::Engines::SDKRegisterCallback sdkRegisterCallback;
    };

    using OnPlayerConnectionCallback = fu2::function<void(flecs::entity, uint64_t) const>;

    class Instance {
      private:
        bool _alive;
        std::chrono::time_point<std::chrono::high_resolution_clock> _nextTick;

        InstanceOptions _opts;

        std::shared_ptr<Scripting::ServerEngine> _scriptingEngine;
        std::shared_ptr<Networking::Engine> _networkingEngine;
        std::shared_ptr<HTTP::Webserver> _webServer;
        std::unique_ptr<External::Firebase::Wrapper> _firebaseWrapper;
        std::unique_ptr<Utils::Config> _fileConfig;
        std::shared_ptr<World::ServerEngine> _worldEngine;

        void InitEndpoints();
        void InitModules();
        void InitNetworkingMessages();
        bool LoadConfigFromJSON();
        void RegisterScriptingBuiltins(void *);

        // managers
        flecs::entity _weatherManager;

        // entity factories
        std::shared_ptr<Shared::Archetypes::PlayerFactory> _playerFactory;
        std::shared_ptr<Shared::Archetypes::StreamingFactory> _streamingFactory;

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

        /*std::shared_ptr<Framework::Scripting::Engine> GetScriptingEngine() const {
            return _scriptingEngine;
        }*/

        std::shared_ptr<World::ServerEngine> GetWorldEngine() const {
            return _worldEngine;
        }

        std::shared_ptr<Networking::Engine> GetNetworkingEngine() const {
            return _networkingEngine;
        }

        std::shared_ptr<HTTP::Webserver> GetWebserver() const {
            return _webServer;
        }

        std::shared_ptr<Shared::Archetypes::PlayerFactory> GetPlayerFactory() const {
            return _playerFactory;
        }

        std::shared_ptr<Shared::Archetypes::StreamingFactory> GetStreamingFactory() const {
            return _streamingFactory;
        }
    };
} // namespace Framework::Integrations::Server
