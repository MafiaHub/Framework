/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/safe_win32.h>

#include <utils/error.h>
#include <utils/result.h>

#include "http/webserver.h"
#include "logging/logger.h"
#include "networking/engine.h"
#include "scripting/module.h"

#include <external/sentry/wrapper.h>

#include <mafianet/types.h>
#include "services/masterlist.h"
#include "utils/config.h"
#include "utils/command_listener.h"
#include "utils/command_processor.h"

#include <utils/lifecycle.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <sig.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Framework::Integrations::Server {
    struct InstanceOptions {

        std::string modSlug;
        std::string modHelpText;
        std::string modName;
        std::string modVersion;
        std::string modConfigFile = "server.json";
        std::string resourcesPath = "resources";

        // Development mode: watch resource files and hot-reload on change.
        // Leave off in production.
        bool developmentMode = false;

        // networked game metadata (required)
        std::string gameName;
        std::string gameVersion;

        bool verifyBuildToken = true; // false bypasses the build/version mismatch challenge

        // Crash reporting (Sentry). Empty DSN leaves it disabled; the value is project-specific.
        std::string sentryDSN;
        // Directory holding crashpad_handler(.exe); the Sentry cache is created beneath it.
        // Empty -> current working directory.
        std::string sentryModulePath;

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

        // update intervals and streaming
        struct WorldConfig {
            float tickInterval = 0.016667f;
            // Interest-grid extent (see Networking::Replication::InterestGrid::Configure): square
            // world bounds and cell size in world units. Defaults match the grid's own — size them to
            // the playable area, or entities beyond the bounds clamp into edge cells and degrade
            // border interest queries.
            float streamCellSize = 100.0f;
            float streamWorldMin = -10000.0f;
            float streamWorldMax = 10000.0f;
        } worldConfig;

        // args
        int argc;
        char **argv;

    };

    // Connection metadata handed to the player-connect callback so the game can create and fully
    // populate the player's avatar (nickname, slot index, identity ids) instead of leaving spawn-time
    // fields at their defaults. Ids are client-reported and unverified; empty when absent.
    struct PlayerConnectionData {
        MafiaNet::PeerGuid guid {};
        uint16_t playerIndex = MafiaNet::UNASSIGNED_PLAYER_INDEX; // the connection's dense slot
        std::string nickname;
        std::string hardwareID;
        std::string steamId;
        std::string discordId;
    };

    class Instance : public Framework::Lifecycle {
      private:
        std::atomic<bool> _shuttingDown;
        // Set after the initial StartAll; gates runtime broadcasts to clients.
        bool _resourcesBooted = false;
        std::chrono::time_point<std::chrono::high_resolution_clock> _nextTick;

        InstanceOptions _opts;

        std::unique_ptr<Scripting::ServerScriptingModule> _scriptingModule;
        std::unique_ptr<Networking::Engine> _networkingEngine;
        std::unique_ptr<HTTP::Webserver> _webServer;
        std::unique_ptr<Utils::Config> _fileConfig;
        std::unique_ptr<Services::MasterlistConnector> _masterlist;
        std::unique_ptr<Utils::CommandListener> _commandListener;
        std::unique_ptr<Utils::CommandProcessor> _commandProcessor;
        std::unique_ptr<External::Sentry::Wrapper> _crashReporter;

        void InitEndpoints();
        void InitNetworkingMessages();
        void InitAssetStreamer();
        // Re-sync a hot-reloaded/started client resource to connected clients.
        void BroadcastResourceRefresh(const std::string &name);
        // Tell connected clients to stop a client resource.
        void BroadcastResourceStop(const std::string &name);
        void InitCommandListener();
        bool LoadConfigFromJSON();
        void RegisterScriptingBuiltins(Framework::Scripting::Engine *);
        
        // Command handlers
        void HandleCommand(std::string_view command);

      public:
        Instance();
        ~Instance();

        [[nodiscard]] Utils::Result<void, Error> Init(InstanceOptions &);
        void Shutdown() override;

        // Override what you need; this is the whole extension surface (no Set*Callback setters).
        // Order: Init -> PostInit -> PostScriptInit; tick: Update -> PostUpdate; Shutdown -> PreShutdown.
        virtual void PostInit() {}
        virtual void PostScriptInit() {}
        virtual void PostUpdate() {}
        virtual void PreShutdown() {}
        virtual void ModuleRegister(Framework::Scripting::Engine *engine) {
            (void)engine;
        }

        virtual void OnPlayerConnect(const PlayerConnectionData &data) {
            (void)data;
        }
        virtual void OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
            (void)guid;
        }
        // Plain lines -> OnChatMessage; '/' lines -> OnChatCommand (command + whitespace-split args).
        virtual void OnChatMessage(uint64_t senderNetworkId, const std::string &text) {
            (void)senderNetworkId, (void)text;
        }
        virtual void OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command, const std::vector<std::string> &args) {
            (void)senderNetworkId, (void)text, (void)command, (void)args;
        }

        // A client emitted a scripted event up (Game.emitServer), sender already resolved. Default
        // dispatches into the client-event table (Events.onClient) as (senderNetworkId, payload);
        // override to hand handlers your own player object. eventName/payloadJson are untrusted.
        virtual void OnClientEvent(uint64_t senderNetworkId, const std::string &eventName, const std::string &payloadJson);

        void Update() override;

        void Run();

        void OnSignal(sig_signal_t);

        // Parse a received chat line and dispatch it to OnChatMessage / OnChatCommand.
        void HandleIncomingChat(uint64_t senderNetworkId, const std::string &text);

        InstanceOptions &GetOptions() {
            return _opts;
        }

        Scripting::ServerScriptingModule *GetScriptingModule() const {
            return _scriptingModule.get();
        }

        External::Sentry::Wrapper *GetCrashReporter() const {
            return _crashReporter.get();
        }

        Networking::Engine *GetNetworkingEngine() const {
            return _networkingEngine.get();
        }

        HTTP::Webserver *GetWebserver() const {
            return _webServer.get();
        }

        // Register a custom command with the command processor
        Utils::Result<std::string, Utils::CommandProcessorError> RegisterCommand(std::string_view name, std::initializer_list<cxxopts::Option> options, const Utils::CommandProc &proc, const std::string &desc) {
            return _commandProcessor->RegisterCommand(name, options, proc, desc);
        }
        Utils::Result<std::string, Utils::CommandProcessorError> RegisterCommand(std::string_view name, std::vector<cxxopts::Option> options, const Utils::CommandProc &proc, const std::string &desc) {
            return _commandProcessor->RegisterCommand(name, options, proc, desc);
        }
        void RemoveCommand(std::string_view name) {
            _commandProcessor->RemoveCommand(name);
        }
    };
} // namespace Framework::Integrations::Server
