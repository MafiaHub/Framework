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
#include "voice/server/voice_server.h"

#include <external/sentry/wrapper.h>

#include <nlohmann/json.hpp>

#include <mafianet/types.h>
#include "services/masterlist.h"
#include "utils/config.h"
#include "utils/config_schema.h"
#include "utils/command_listener.h"
#include "utils/command_processor.h"

#include <utils/crypto.h>
#include <utils/lifecycle.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <sig.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace v8 {
    class Isolate;
    class Value;
    template <class T>
    class Local;
} // namespace v8

namespace Framework::Integrations::Server {
    struct InstanceOptions {

        std::string modSlug;
        std::string modHelpText;
        std::string modName;
        std::string modVersion;
        std::string modConfigFile = "server.json";
        // Mod-declared server.json keys, read from the file's "mod" object. Validated at Init, so a
        // bad config fails before the port opens rather than at the first connect. Fields marked
        // replicated are published to clients during the MafiaNet session handshake, which lands
        // before the asset phase and before the client reports a connection.
        Framework::Utils::ConfigSchema modConfigSchema;
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
        // Release identifier; empty -> derived from gameName + gameVersion.
        std::string sentryRelease;
        // Deployment environment ("retail" / "dev" / "ci"); empty leaves it unset.
        std::string sentryEnvironment;
        // Extra files registered before sentry_init.
        std::vector<std::string> sentryAttachments;

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
        int32_t maxPlayersHardCap = 0; // compiled-in ceiling the config file cannot raise; 0 = uncapped
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
        // Resolved mod config and the subset that goes on the wire. Held separately so the
        // replicated view cannot drift from what was declared replicated.
        nlohmann::json _modConfig;
        std::string _replicatedModConfig;
        std::unique_ptr<Services::MasterlistConnector> _masterlist;
        std::unique_ptr<Utils::CommandListener> _commandListener;
        std::unique_ptr<Utils::CommandProcessor> _commandProcessor;
        // Not owned; the reporter is process-wide.
        External::Sentry::Wrapper *_crashReporter = nullptr;
        // Proximity voice relay. Value member: it holds no resources until Init attaches it to
        // the peer, so a mod that never enables voice pays nothing beyond the empty maps.
        Voice::VoiceServer _voiceServer;
        // Reused every tick so the drain never allocates.
        std::vector<Voice::TalkingChange> _voiceTalkingChanges;
        std::unordered_set<uint64_t> _armedSpawnBarrierGuids;
        std::unordered_set<uint64_t> _readyPlayerGuids;

        // Loaded from (or written to) the staging directory; sent with the resource list.
        Utils::Crypto::Key _packageKey {};
        std::string _packageKeyHex;
        bool _packageKeyReady = false;
        std::unordered_map<std::string, std::string> _packageHashes;

        void InitEndpoints();
        void InitNetworkingMessages();
        void InitAssetStreamer();
        // Directory the built .fwpak containers are staged in for the streamer to upload.
        std::string GetPackageStagingDir() const;
        // Re-sync a hot-reloaded/started client resource to connected clients.
        void BroadcastResourceRefresh(const std::string &name);
        // Tell connected clients to stop a client resource.
        void BroadcastResourceStop(const std::string &name);
        void InitCommandListener();
        bool LoadConfigFromJSON();
        // Publish the replicated subset as this peer's session payload. Called once, before the
        // networking engine starts accepting connections.
        void PublishSessionConfig();
        // server.json as it would be written on a first run: framework keys from the current
        // options, mod keys from the schema defaults.
        nlohmann::json BuildDefaultConfigFile() const;
        void RegisterScriptingBuiltins(Framework::Scripting::Engine *);
        
        void HandleCommand(std::string_view command);
        void EmitConsoleCommand(const std::string &command, const std::vector<std::string> &args);
        // Runs once per tick, after the relay's own update has retired the talkers who went quiet.
        void DispatchVoiceTalkingChanges();

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
        virtual void OnPlayerReady(MafiaNet::PeerGuid guid) {
            (void)guid;
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
        // A player started or stopped talking; also emitted as the "playerVoiceStart" /
        // "playerVoiceStop" events. Derived from relayed frames, so it tracks speech rather than
        // the push-to-talk key, and the stop trails the last frame by kTalkingTimeoutMs. A
        // disconnect ends talking without a stop; clean up on OnPlayerDisconnect too.
        virtual void OnPlayerVoiceStateChanged(uint64_t networkId, bool talking) {
            (void)networkId, (void)talking;
        }

        // A console line no built-in command claimed; also emitted as the "consoleCommand" event.
        virtual void OnConsoleCommand(const std::string &text, const std::string &command, const std::vector<std::string> &args) {
            (void)text, (void)command, (void)args;
        }

        // A client emitted a scripted event up (Events.emitServer); dispatch it to Events.onClient as
        // (player, payload). Override WrapScriptPlayer to change the player object.
        virtual void OnClientEvent(uint64_t senderNetworkId, const std::string &eventName, const std::string &payloadJson);

        // The player object passed to onClient handlers. Default is the base Player builtin.
        virtual v8::Local<v8::Value> WrapScriptPlayer(v8::Isolate *isolate, uint64_t networkId);

        void Update() override;

        void Run();

        void OnSignal(sig_signal_t);

        // Parse a received chat line and dispatch it to OnChatMessage / OnChatCommand.
        void HandleIncomingChat(uint64_t senderNetworkId, const std::string &text);

        InstanceOptions &GetOptions() {
            return _opts;
        }

        // The resolved "mod" object: declared defaults filled in, types and allowed values already
        // checked, so a declared key always reads back as its declared type.
        Framework::Utils::ConfigView GetModConfig() const {
            return Framework::Utils::ConfigView(&_modConfig);
        }

        Scripting::ServerScriptingModule *GetScriptingModule() const {
            return _scriptingModule.get();
        }

        External::Sentry::Wrapper *GetCrashReporter() const {
            return _crashReporter;
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
