/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/packet_thread.h>
#include <networking/replication/replication_manager.h>
#include <networking/rpc/rpc_identifier.h>

#include <utils/safe_win32.h>

#include <utils/error.h>
#include <utils/result.h>

#include "asset_file_server.h"
#include "asset_paks/asset_pak_builder.h"
#include "client_package.h"
#include "connection_gate.h"
#include "http/webserver.h"
#include "logging/logger.h"
#include "networking/engine.h"
#include "script_thread.h"
#include "scripting/module.h"
#include "tick_breakdown.h"
#include "voice/server/voice_server.h"

#include <external/sentry/wrapper.h>

#include <nlohmann/json.hpp>

#include "services/masterlist.h"
#include "utils/command_listener.h"
#include "utils/command_processor.h"
#include "utils/config.h"
#include "utils/config_schema.h"
#include <mafianet/types.h>

#include <utils/crypto.h>
#include <utils/fork_join_pool.h>
#include <utils/lifecycle.h>
#include <utils/task_scheduler.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
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

namespace cxxopts {
    class Options;
    class ParseResult;
} // namespace cxxopts

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

        // A script handler, listener, export call or resource tick that spends at least this long in its own
        // script logs a warning naming the resource (server.json "slow-script-ms"; 0 turns the warnings off).
        int32_t slowScriptWarnMs = 50;

        // An RPC handler that holds the packet thread at least this long logs a warning, at most once a second
        // (server.json "slow-packet-handler-ms"; 0 turns the warnings off). It delays the packets behind it.
        int32_t slowPacketHandlerWarnMs = 10;

        // Threads beside the tick thread (server.json "replication-threads" and "background-threads", or
        // the flags of the same names). Negative, the default, sizes them to the host's cores (see
        // Utils::ThreadBudget); 0 keeps that work on the tick thread.
        // Replication: interest and the serialize pass, and the game's own per-tick loops.
        int32_t replicationThreads = -1;
        // Background: blocking work such as packaging a hot-reloaded resource.
        int32_t backgroundThreads = -1;

        // What a resource's asset paks may carry, one lane per folder (`<resource>/<lane>/`). Null
        // ships none. See AssetPakBuilder.
        std::shared_ptr<const Utils::StreamedAssets::Policy> assetPakPolicy;

        // networked game metadata (required)
        std::string gameName;
        std::string gameVersion;

        bool verifyBuildToken = true; // false bypasses the build/version mismatch challenge

        // Deployment salt keying every RPC identifier (see networking/rpc/rpc_identifier.h). Must be
        // the same on client and server; a release build sets it from its own secret.
        uint64_t rpcIdentifierSalt = Framework::Networking::RPC::kDefaultIdentifierSalt;

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

        // http(s) base URLs clients fetch resource packages and asset paks from (<url>/<file>), such as
        // CDNs the operator mirrors the staged files (and their .chunks lists) to; several are separated
        // by whitespace or commas and tried in order. Clients fall back to the game port. Empty: the
        // game port only.
        std::string assetDownloadUrl;

        // Connections admitted beyond maxPlayers to download and start resources: a player takes one of
        // the maxPlayers slots only when it joins the world, so a join burst does not fill the server
        // with downloads. 0 keeps every admitted connection on a player slot.
        int32_t maxDownloading = 0;

        // Secret the resource package key is derived from (with modSlug). Empty: the server token
        // (bindSecretKey). Changing it changes every package hash, so clients download them again.
        std::string packageSecret;
        bool bindPublicServer = true;

        // How long playerConnecting handlers may take to settle before the connection is turned
        // away. Each PendingConnection.update() restarts it for that connection.
        int32_t admissionTimeoutMs = 30000;

        // Connections waiting on playerConnecting are held by MafiaNet in a pool of their own, beside
        // the maxPlayers slots: they take no player slot and are not counted as online. The pool bounds
        // how many may wait at once, and how many of those one IP address may hold (0: unbounded).
        uint16_t pendingConnections           = 32;
        uint16_t pendingConnectionsPerAddress = 4;

        // MafiaHub Services
        struct Services {
            std::string apiUrl        = "https://api.mafiahub.dev";
            std::string masterlistUrl = "";
        } services;

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

    // Settings resolve lowest to highest: InstanceOptions defaults, then the config document, then
    // the command line. Each layer only touches the keys it actually carries.
    // The resource package key: stable for the same mod slug and secret, so it needs no storage.
    Framework::Utils::Crypto::Key DerivePackageKey(const InstanceOptions &opts);
    void AddCommandLineOptions(cxxopts::Options &options, const InstanceOptions &opts);
    void ApplyConfigDocument(const nlohmann::json &document, InstanceOptions &opts);
    void ApplyCommandLine(const cxxopts::ParseResult &result, InstanceOptions &opts);

    // Connection metadata handed to the player-connect callback so the game can create and fully
    // populate the player's avatar (nickname, slot index, identity ids) instead of leaving spawn-time
    // fields at their defaults. Ids are client-reported and unverified; empty when absent.
    struct PlayerConnectionData {
        MafiaNet::PeerGuid guid {};
        uint16_t playerIndex = MafiaNet::UNASSIGNED_PLAYER_INDEX; // the connection's dense slot
        std::string nickname;
        std::string hardwareID;
        std::string steamId;
        std::string epicId;
        std::string discordId;
        std::string discordName;
    };

    class Instance: public Framework::Lifecycle {
      private:
        std::atomic<bool> _shuttingDown;
        // Raised by signals and console commands, read by Run(). Neither may
        // tear modules down while a tick still uses them; the caller shuts down
        // on the main thread after Run() returns.
        std::atomic<bool> _stopRequested = false;
        // Set after the initial StartAll; gates runtime broadcasts to clients.
        bool _resourcesBooted = false;
        std::chrono::steady_clock::time_point _nextTick {};
        std::chrono::steady_clock::time_point _lastHitchWarnAt {};
        uint32_t _suppressedHitches = 0;
        TickBreakdown _lastTick;

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
        std::vector<Voice::TierChange> _voiceTierChanges;
        // Where each connection is on its way into the world. Written where packets are handled and
        // read by admission, where scripts run: under this mutex, which is held across no call.
        mutable std::mutex _connectionsMutex;
        std::unordered_set<uint64_t> _armedSpawnBarrierGuids;
        std::unordered_set<uint64_t> _readyPlayerGuids;
        // Connections that joined the world (ClientJoin): the ones holding a player slot.
        std::unordered_set<uint64_t> _joinedGuids;

        // Holds each authenticated connection until the playerConnecting handlers answer.
        ConnectionGate _connectionGate;
        // Reused every tick so the drain never allocates.
        std::vector<AdmissionDecision> _admissionDecisions;

        // Loaded from (or written to) the staging directory; sent with the resource list.
        Utils::Crypto::Key _packageKey {};
        std::string _packageKeyHex;
        bool _packageKeyReady = false;
        std::unordered_map<std::string, std::string> _packageHashes;
        std::unordered_map<std::string, uint64_t> _packageSizes;
        AssetFileServer _assetFileServer;

        // Each running resource's asset paks, staged and served beside the packages.
        AssetPakBuilder _assetPakBuilder;
        std::unordered_map<std::string, std::vector<Framework::Networking::RPC::AssetPakInfo>> _assetPaks;

        // What the resource list says about one resource: its package hash and asset paks.
        Framework::Networking::RPC::ResourceInfo DescribeResource(const std::string &name, const std::string &version) const;
        // Every running resource a client needs: those with client scripts, and those shipping assets only.
        std::vector<Framework::Networking::RPC::ResourceInfo> DescribeClientResources() const;

        void InitEndpoints();
        void InitNetworkingMessages();
        // Calls a mod's override where a packet is handled. One that throws is logged and the packet
        // thread carries on with what the Framework does next for that connection: thrown out of
        // RakPeer::Receive it would end the thread and the process.
        static void GuardOverride(const char *name, uint64_t guid, const fu2::function<void() const> &call);
        // A connection request arrived: hand it to the admission gate, or admit it at once when no
        // script gates connections. Refused outright when it carries no identity.
        void OnSessionRequest(MafiaNet::RakNetGUID guid, const std::optional<Framework::Networking::RPC::ClientIdentity> &identity);
        // Let a waiting request in, if a player slot is free; the connection then becomes real on both
        // sides and the usual build check, resource list and join follow.
        void AdmitSession(MafiaNet::RakNetGUID guid);
        // Runs once per tick, after scripting: acts on what the gate settled.
        void ApplyAdmissionDecisions();
        void PublishClientAssets();
        // What the asset file server serves: published name -> staged file. Kept so one resource
        // can be republished without rebuilding the rest.
        std::unordered_map<std::string, std::filesystem::path> _servedFiles;
        // The resources clients never receive, as of the last full publish. A change to this set
        // changes other packages' manifests, so it decides whether a single republish is enough.
        std::set<std::string> _serverOnlyResources;

        std::set<std::string> CollectServerOnlyResources() const;

        // Publishing one resource to clients is cut in three so the slow part can leave the tick
        // thread: what to build is read off the resource here (Plan), built anywhere (RunClientPublish:
        // its package beside the served one, its asset paks under new names), and only then recorded,
        // put in place and served, here again (Record). Nothing the server has announced changes
        // before Record.
        struct ClientPublishJob {
            std::string name;
            std::optional<ClientPackageRequest> package;
            std::optional<std::filesystem::path> pakFolder;
        };
        struct ClientPublish {
            bool wantsPackage = false;
            ClientPackage package;
            bool wantsPaks = false;
            // The paks could not be built: not the same as a resource that ships none. What it served before stays.
            bool paksFailed = false;
            AssetPakBuilder::Prepared paks;
        };
        ClientPublishJob PlanClientPublish(const std::string &name, const std::string &stagingDir);
        // Numbers the builds, so two of one resource never share the file they write.
        uint64_t _publishSerial = 0;
        static ClientPublish RunClientPublish(const ClientPublishJob &job, const AssetPakBuilder &paks);
        void RecordClientPublish(const std::string &name, ClientPublish built, size_t &packagedFiles);
        // After one resource (re)started at runtime: rebuilt alone on a background thread when nothing
        // else can have changed, and announced to clients once it is served; the whole set, at once,
        // otherwise.
        void RepublishClientResource(const std::string &name);
        void FinishRepublish(const std::string &name, ClientPublish built);
        // Tell connected clients a resource's files changed. Only after they are served.
        void AnnounceResourceRefresh(const std::string &name);
        // While everything is published again: what each resource's paks were before, to put back for a
        // resource whose paks could not be rebuilt. Empty otherwise.
        std::unordered_map<std::string, std::vector<Framework::Networking::RPC::AssetPakInfo>> _assetPaksBefore;
        // Resources being rebuilt in the background, and those that changed again meanwhile.
        std::unordered_set<std::string> _publishing;
        std::unordered_set<std::string> _publishAgain;
        // Directory the built .fwpak containers are staged in for the streamer to upload.
        std::string GetPackageStagingDir() const;
        // A client resource started or was hot-reloaded: republish it, then re-sync connected clients.
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
        // Runs once per tick, after the relay's own update has retired the talkers who went
        // quiet: talking edges, and the tier switches players asked for.
        void DispatchVoiceChanges();
        // Subscription raising entityStateChange; released on shutdown.
        Framework::Networking::Replication::StateChangeHandle _stateBagEvents = Framework::Networking::Replication::kInvalidStateChangeHandle;

        // The thread scripts run on. A tick has the world from its first line to its last, and so does
        // the thread that runs Init() and Shutdown().
        std::unique_ptr<ScriptThread> _scriptThread;
        bool _worldHeld = false;
        // The thread packets are handled on: RPC handlers, inbound entity state, connections opening
        // and closing. It does not take the world; see Networking::PacketThread.
        Framework::Networking::PacketThread _packetThread;
        // What a tick owes scripts, once per tick interval, on their thread.
        void ScriptRound();
        // The scripting half of Init(), on the script thread. Empty on success.
        std::string InitScripting(Framework::Networking::Replication::ReplicationManager *replication);

        // Declared last, so they are stopped first: background work reads the asset pak builder above.
        // The replication pass and the game's own per-tick loops share the workers; the tick thread
        // is one of their lanes.
        std::unique_ptr<Utils::ForkJoinPool> _tickWorkers;
        std::unique_ptr<Utils::TaskScheduler> _scheduler;

      public:
        Instance();
        ~Instance();

        [[nodiscard]] Utils::Result<void, Error> Init(InstanceOptions &);
        void Shutdown() override;

        // Where the last tick's time went, by phase. The slow-tick warning prints it.
        const TickBreakdown &GetLastTickBreakdown() const {
            return _lastTick;
        }

        // Override what you need; this is the whole extension surface (no Set*Callback setters).
        // Order: Init -> PostInit -> PostScriptInit; tick: Update -> PostUpdate; Shutdown -> PreShutdown.
        virtual void PostInit() {}
        virtual void PostScriptInit() {}
        virtual void PostUpdate() {}
        virtual void PreShutdown() {}
        // Called for every scripting runtime as it is set up: each resource's, when it starts, and
        // the tick and packet threads' own. It declares bindings, and builds the mod's handle classes
        // (T::GetClass(isolate)) so that wrapping an entity later is a lookup. It runs under the lock
        // that keeps binding declarations apart (Scripting::CatalogWriteLock): it must not ask the
        // world for a handle, and should run no script.
        virtual void ModuleRegister(Framework::Scripting::Engine *engine) {
            (void)engine;
        }

        // The five below are called where the packet that caused them is handled, on the packet
        // thread, with no entity held: an override reaches entities through `world`, by handle
        // (networking/replication/world.h), and anything else it touches guards itself.
        virtual void OnPlayerConnect(const PlayerConnectionData &data, Framework::Networking::Replication::World &world) {
            (void)data, (void)world;
        }
        virtual void OnPlayerReady(MafiaNet::PeerGuid guid, Framework::Networking::Replication::World &world) {
            (void)guid, (void)world;
        }
        virtual void OnPlayerDisconnect(MafiaNet::PeerGuid guid, Framework::Networking::Replication::World &world) {
            (void)guid, (void)world;
        }
        // Plain lines -> OnChatMessage; '/' lines -> OnChatCommand (command + whitespace-split args).
        virtual void OnChatMessage(uint64_t senderNetworkId, const std::string &text, Framework::Networking::Replication::World &world) {
            (void)senderNetworkId, (void)text, (void)world;
        }
        virtual void OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command, const std::vector<std::string> &args, Framework::Networking::Replication::World &world) {
            (void)senderNetworkId, (void)text, (void)command, (void)args, (void)world;
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

        // The entity object passed to entityStateChange handlers, for any replicated entity rather
        // than a player. Default is the base Entity builtin; a game with handles of its own overrides
        // this to hand scripts the specific one.
        virtual v8::Local<v8::Value> WrapScriptEntity(v8::Isolate *isolate, uint64_t networkId);

        void Update() override;

        void Run();

        void OnSignal(sig_signal_t);

        // Parse a received chat line and dispatch it to OnChatMessage / OnChatCommand.
        void HandleIncomingChat(uint64_t senderNetworkId, const std::string &text, Framework::Networking::Replication::World &world);

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

        // Scripts run on a thread of their own. Code on the tick thread (PostUpdate) and on the packet
        // thread (RPC handlers, the On* overrides above) raises events as it always did, and they are
        // carried to that thread; for anything else that ends in script, this runs `task` there, with
        // the world: it may open an EngineScope, call into resources and touch the world like tick
        // code. Before Init() and after Shutdown() it runs at once.
        void PostToScripts(Utils::TaskScheduler::Task task);
        // The same, and waits for it. From the tick thread the world may have changed when it returns.
        // Not from inside a handle's scope: the task may need the entity held, so the call logs and
        // aborts, as EmitReservedSync does. Let go first (Ref::Reset), or use PostToScripts.
        void RunOnScriptThread(Utils::TaskScheduler::Task task);

        // Blocking work goes here instead of into the tick: RunInBackground for the work, with a
        // completion that runs on the tick thread, at the top of a later tick.
        Utils::TaskScheduler *GetScheduler() const {
            return _scheduler.get();
        }

        // For a loop of independent iterations inside PostUpdate: Run() spreads it over the
        // replication workers and returns when every iteration has run.
        Utils::ForkJoinPool *GetTickWorkers() const {
            return _tickWorkers.get();
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

        /** The asset paks the server ships, to check a path a script names against them. */
        const AssetPakBuilder &GetAssetPaks() const {
            return _assetPakBuilder;
        }
    };
} // namespace Framework::Integrations::Server
