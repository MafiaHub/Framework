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
#include "http/webserver.h"
#include "logging/logger.h"
#include "networking/engine.h"
#include "scripting/module.h"
#include "services/masterlist.h"
#include "utils/config.h"
#include "utils/command_listener.h"
#include "utils/command_processor.h"
#include "world/server.h"

#include <flecs.h>

#include "world/types/player.hpp"
#include "world/types/streaming.hpp"

#include <utils/lifecycle.h>

#include <atomic>
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
        std::string resourcesPath = "resources";

        // networked game metadata (required)
        std::string gameName;
        std::string gameVersion;

        std::string bindHost;
        std::string bindSecretKey;
        std::string bindMapName;
        int32_t bindPort;
        std::string bindPassword;
        bool bindPublicServer = true;

        // MafiaHub Services
        struct Services {
            std::string apiUrl = "https://api.mafiahub.dev";
            std::string masterlistUrl = "";
        } services;

        bool webServerEnabled = true;
        std::string webBindHost;
        int32_t webBindPort;

        int32_t maxPlayers;
        std::string httpServeDir;

        bool enableSignals;

        // update intervals
        Framework::World::ServerEngine::ServerConfig worldConfig;

        // args
        int argc;
        char **argv;

    };

    using OnPlayerConnectionCallback = fu2::function<void(flecs::entity, uint64_t) const>;

    class Instance : public Framework::Lifecycle {
      private:
        std::atomic<bool> _shuttingDown;
        std::chrono::time_point<std::chrono::high_resolution_clock> _nextTick;

        InstanceOptions _opts;

        std::unique_ptr<Scripting::ServerScriptingModule> _scriptingModule;
        std::unique_ptr<Networking::Engine> _networkingEngine;
        std::unique_ptr<HTTP::Webserver> _webServer;
        std::unique_ptr<Utils::Config> _fileConfig;
        std::shared_ptr<World::ServerEngine> _worldEngine;
        std::unique_ptr<Services::MasterlistConnector> _masterlist;
        std::unique_ptr<Utils::CommandListener> _commandListener;
        std::unique_ptr<Utils::CommandProcessor> _commandProcessor;

        void InitEndpoints();
        void InitModules() const;
        void InitNetworkingMessages() const;
        void InitAssetStreamer();
        void InitCommandListener();
        bool LoadConfigFromJSON();
        void RegisterScriptingBuiltins(Framework::Scripting::Engine *);
        
        // Command handlers
        void HandleCommand(const std::string &command);

        // managers
        flecs::entity _weatherManager;

        // entity factories
        std::unique_ptr<World::Archetypes::PlayerFactory> _playerFactory;
        std::unique_ptr<World::Archetypes::StreamingFactory> _streamingFactory;

        // callbacks
        OnPlayerConnectionCallback _onPlayerConnectCallback;
        OnPlayerConnectionCallback _onPlayerDisconnectCallback;

      public:
        Instance();
        ~Instance();

        [[nodiscard]] ServerError Init(InstanceOptions &);
        void Shutdown() override;

        virtual void PostInit() {}
        
        virtual void PostScriptInit() {}
        
        virtual void PostUpdate() {}

        virtual void PreShutdown() {}

        virtual void ModuleRegister(Framework::Scripting::Engine *engine) {
            (void)engine;
        }

        void Update() override;

        void Run();

        void OnSignal(sig_signal_t);

        void SetOnPlayerConnectCallback(OnPlayerConnectionCallback onPlayerConnectCallback) {
            _onPlayerConnectCallback = std::move(onPlayerConnectCallback);
        }

        void SetOnPlayerDisconnectCallback(OnPlayerConnectionCallback onPlayerDisconnectCallback) {
            _onPlayerDisconnectCallback = std::move(onPlayerDisconnectCallback);
        }

        InstanceOptions &GetOpts() {
            return _opts;
        }

        Scripting::ServerScriptingModule *GetScriptingModule() const {
            return _scriptingModule.get();
        }

        std::shared_ptr<World::ServerEngine> GetWorldEngine() const {
            return _worldEngine;
        }

        Networking::Engine *GetNetworkingEngine() const {
            return _networkingEngine.get();
        }

        HTTP::Webserver *GetWebserver() const {
            return _webServer.get();
        }

        World::Archetypes::PlayerFactory *GetPlayerFactory() const {
            return _playerFactory.get();
        }

        World::Archetypes::StreamingFactory *GetStreamingFactory() const {
            return _streamingFactory.get();
        }
        
        // Register a custom command with the command processor
        Utils::Result<std::string, Utils::CommandProcessorError> RegisterCommand(const std::string &name, std::initializer_list<cxxopts::Option> options, const Utils::CommandProc &proc, const std::string &desc) {
            return _commandProcessor->RegisterCommand(name, options, proc, desc);
        }
        Utils::Result<std::string, Utils::CommandProcessorError> RegisterCommand(const std::string &name, std::vector<cxxopts::Option> options, const Utils::CommandProc &proc, const std::string &desc) {
            return _commandProcessor->RegisterCommand(name, options, proc, desc);
        }
        void RemoveCommand(const std::string &name) {
            _commandProcessor->RemoveCommand(name);
        }
    };
} // namespace Framework::Integrations::Server
