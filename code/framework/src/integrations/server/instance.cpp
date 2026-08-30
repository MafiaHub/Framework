/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include <filesystem>
#include <set>
#include <fstream>
#include <sstream>

#include "core_modules.h"

#include "integrations/shared/rpc/emit_script_event.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "networking/rpc/chat_message.h"
#include "networking/rpc/client_identity.h"
#include "networking/rpc/resource_refresh.h"
#include "networking/rpc/server_resources.h"
#include "networking/rpc/voice_settings.h"

#include "networking/connection.h"

#include "scripting/builtins/events.h"
#include "scripting/builtins/player.h"
#include "scripting/node_engine.h"
#include "scripting/resource/resource_manager.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <v8pp/convert.hpp>

#include "utils/command_processor.h"
#include "utils/config_schema.h"
#include "utils/path.h"
#include "utils/profiler.h"
#include "utils/version.h"
#include "utils/time.h"

#include "cxxopts.hpp"
#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <csignal>

namespace Framework::Integrations::Server {

    Instance::Instance(): _shuttingDown(false) {
        _networkingEngine = std::make_unique<Networking::Engine>();
        _webServer        = std::make_unique<HTTP::Webserver>();
        _fileConfig       = std::make_unique<Utils::Config>();
        _scriptingModule  = std::make_unique<Scripting::ServerScriptingModule>();
        _masterlist       = std::make_unique<Services::MasterlistConnector>();
        _commandListener  = std::make_unique<Utils::CommandListener>();
        _commandProcessor = std::make_unique<Utils::CommandProcessor>();
        _crashReporter    = &External::Sentry::GetCrashReporter();
    }

    Instance::~Instance() {
        sig_detach(this);
    }

    Utils::Result<void, Error> Instance::Init(InstanceOptions &opts) {
        _opts = opts;

        if (opts.gameName.empty() || opts.gameVersion.empty()) {
            return Error("Game name and version are required");
        }

        // Crash reporting comes up first so its handler is installed before anything else can fault.
        // An entry-point InitCrashReporter already installed it; this is then a no-op and only the
        // decoration below applies.
        if (_crashReporter && !opts.sentryDSN.empty()) {
            External::Sentry::InitOptions sentryOpts;
            sentryOpts.dsn         = opts.sentryDSN;
            sentryOpts.handlerPath = opts.sentryModulePath.empty() ? "." : opts.sentryModulePath;
            sentryOpts.release     = opts.sentryRelease.empty() ? opts.gameName + "@" + opts.gameVersion : opts.sentryRelease;
            sentryOpts.environment = opts.sentryEnvironment;
            sentryOpts.attachments = opts.sentryAttachments;
            if (auto sentryResult = External::Sentry::InitCrashReporter(sentryOpts); !sentryResult) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Crash reporting disabled: {}", sentryResult.GetError().message);
            }
            else {
                _crashReporter->SetGameInformation({opts.gameName, opts.gameVersion + " / mod " + opts.modVersion});
                _crashReporter->SetTag("net.role", "server");
                _crashReporter->SetTag("build.game_version", opts.gameVersion);
                _crashReporter->SetTag("build.mod_version", opts.modVersion);

                auto *reporter = _crashReporter;
                Logging::GetInstance()->SetLogForwarder([reporter](int level, const std::string &name, const std::string &message) {
                    External::Sentry::Level mapped = External::Sentry::Level::Info;
                    if (level >= spdlog::level::critical) {
                        mapped = External::Sentry::Level::Fatal;
                    }
                    else if (level >= spdlog::level::err) {
                        mapped = External::Sentry::Level::Error;
                    }
                    else if (level >= spdlog::level::warn) {
                        mapped = External::Sentry::Level::Warning;
                    }
                    reporter->AddBreadcrumb(name, message, mapped);
                });

                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Crash reporting initialized");
            }
        }

        CoreModules::SetTickInterval(opts.worldConfig.tickInterval);

        // First level is argument parser, because we might want to overwrite stuffs
        cxxopts::Options options(_opts.modSlug, _opts.modHelpText);
        options.allow_unrecognised_options();
        options.add_options("MafiaHub Integrations server",
            {{"p,port", "Networking port to bind", cxxopts::value<int32_t>()->default_value(std::to_string(_opts.bindPort))}, {"h,host", "Networking host to bind", cxxopts::value<std::string>()->default_value(_opts.bindHost)},
                {"c,config", "JSON config file to read", cxxopts::value<std::string>()->default_value(_opts.modConfigFile)}, {"P,apiport", "HTTP API port to bind", cxxopts::value<int32_t>()->default_value(std::to_string(_opts.webBindPort))},
                {"H,apihost", "HTTP API host to bind", cxxopts::value<std::string>()->default_value(_opts.webBindHost)}, {"help", "Prints this help message", cxxopts::value<bool>()->default_value("false")}});

        // Try to parse and return if anything wrong happened
        const auto result = options.parse(_opts.argc, _opts.argv);

        // If help was specified, just print the help and exit
        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            exit(0);
        }

        // Allow mod to specify custom JSON config file name
        _opts.modConfigFile = result["config"].as<std::string>();

        // Load JSON config if present
        if (!LoadConfigFromJSON()) {
            return Error("Failed to parse JSON config file '" + _opts.modConfigFile + "'");
        }

        // Finally apply back to the structure that is used everywhere the settings from the parser
        _opts.bindHost = result["host"].as<std::string>();
        _opts.bindPort = result["port"].as<int32_t>();

        if (_opts.bindHost.empty()) {
            return Error("bindHost is required");
        }
        if (_opts.bindPort <= 0 || _opts.bindPort > 65535) {
            return Error("bindPort must be in the range 1-65535 (got " + std::to_string(_opts.bindPort) + ")");
        }
        if (_opts.maxPlayers <= 0) {
            return Error("maxPlayers must be greater than 0 (got " + std::to_string(_opts.maxPlayers) + ")");
        }

        // Initialize the logging instance with the mod slug name
        Logging::GetInstance()->SetLogName(_opts.modSlug);

        // Initialize the web server
        if (_opts.webServerEnabled && _webServer->Init(_opts.webBindHost, _opts.webBindPort, _opts.httpServeDir) != HTTP::WebserverError::WEBSERVER_NONE) {
            return Error("Failed to initialize the webserver on " + _opts.webBindHost + ":" + std::to_string(_opts.webBindPort));
        }

        // Initialize our networking engine
        if (auto netResult = _networkingEngine->Init(_opts.bindHost, _opts.bindPort, _opts.maxPlayers, _opts.bindPassword); !netResult) {
            return netResult;
        }

        CoreModules::SetNetworkPeer(_networkingEngine->GetNetworkServer());

        // Before any peer can connect, so the very first handshake already carries it.
        PublishSessionConfig();

        // The networked world is the replication manager owned by the peer. Serialize entity updates
        // at the configured tick rate (tickInterval is in seconds).
        auto *replication = _networkingEngine->GetNetworkServer()->GetReplicationManager();
        CoreModules::SetReplication(replication);
        if (replication) {
            replication->SetAutoSerializeInterval(static_cast<MafiaNet::Time>(Utils::Time::SecondsToMs(_opts.worldConfig.tickInterval)));
            replication->ConfigureGrid(_opts.worldConfig.streamCellSize, _opts.worldConfig.streamWorldMin, _opts.worldConfig.streamWorldMax);
            // Replication owns connection teardown: when a peer drops, it notifies the game (avatar
            // still resolvable) just before destroying and broadcasting the destruction of the avatar.
            replication->SetOnClientDisconnect([this](MafiaNet::PeerGuid guid) {
                if (_readyPlayerGuids.contains(static_cast<uint64_t>(guid))) {
                    OnPlayerDisconnect(guid);
                }
            });
        }

        // Voice relay: attaches RakVoice to the live peer, so it must come up after the networking
        // engine. Failure is not fatal — the server simply runs without voice.
        if (_voiceServer.Init(_networkingEngine->GetNetworkServer())) {
            CoreModules::SetVoiceServer(&_voiceServer);
        }
        else {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Voice relay unavailable; voice chat disabled");
        }

        if (!_opts.bindPublicServer || !_masterlist->Init(_opts.services.masterlistUrl, _opts.bindSecretKey)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Server will not be announced to masterlist");
        }

        // Init the signals handlers if enabled
        if (_opts.enableSignals) {
            sig_attach(SIGINT, sig_slot(this, &Instance::OnSignal), sig_ctx_sys());
            sig_attach(SIGTERM, sig_slot(this, &Instance::OnSignal), sig_ctx_sys());
        }

        // Register the default endpoints
        InitEndpoints();

        // Initialize default messages
        InitNetworkingMessages();

        // Initialize command listener
        InitCommandListener();

        // Initialize mod subsystems
        PostInit();
    
        const auto sdkCallback = [this](Framework::Scripting::Engine *engine) {
            this->RegisterScriptingBuiltins(engine);
        };

        // Initialize the scripting engine
        _scriptingModule->SetResourcesPath(_opts.resourcesPath);
        _scriptingModule->SetDevMode(_opts.developmentMode);
        if (_scriptingModule->Init(sdkCallback) != Framework::Scripting::ScriptingError::SCRIPTING_NONE) {
            return Error("Failed to initialize the scripting engine");
        }

        CoreModules::SetScriptingModule(_scriptingModule.get());

        // A resource owns the entities it spawns; they are destroyed when it stops.
        if (replication) {
            auto *resourceManager = _scriptingModule->GetResourceManager();
            replication->SetOnEntityCreated([resourceManager](uint64_t networkId) {
                resourceManager->OnEntityCreated(networkId);
            });
            replication->SetOnEntityDestroyed([resourceManager](uint64_t networkId) {
                resourceManager->OnEntityDestroyed(networkId);
            });
        }

        PostScriptInit();

        // Discover resources
        _scriptingModule->GetResourceManager()->DiscoverResources();

        // Initialize asset streamer (needs discovered resources to know client files)
        InitAssetStreamer();

        // Mirror runtime resource start/stop to clients. Gated on boot (the
        // StartAll below predates any connection) and shutdown.
        _scriptingModule->GetResourceManager()->SetOnResourceStarted([this](const std::string &name) {
            if (_resourcesBooted && !_shuttingDown) {
                BroadcastResourceRefresh(name);
            }
        });
        _scriptingModule->GetResourceManager()->SetOnResourceStopped([this](const std::string &name) {
            if (_resourcesBooted && !_shuttingDown) {
                BroadcastResourceStop(name);
            }
        });

        // Start all resources (ES modules load asynchronously via normal Update cycle)
        auto startResult = _scriptingModule->GetResourceManager()->StartAll();
        if (!startResult) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to start resources: {}", startResult.GetError());
        }
        _resourcesBooted = true;

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->flush();
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Host:\t{}", _opts.bindHost);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Port:\t{}", _opts.bindPort);
        if (_opts.webServerEnabled) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Http Host:\t{}", _opts.webBindHost);
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Http Port:\t{}", _opts.webBindPort);
        }
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Max Players:\t{}", _opts.maxPlayers);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("{} Server successfully started", _opts.modName);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->flush();

        _initialized  = true;
        _shuttingDown = false;
        return {};
    }

    void Instance::InitEndpoints() {
        _webServer->RegisterRequest("/", [this](const httplib::Request &req, httplib::Response &res) {
            nlohmann::json root;
            root["mod_name"]          = _opts.modName;
            root["mod_slug"]          = _opts.modSlug;
            root["mod_version"]       = _opts.modVersion;
            root["framework_version"] = Utils::Version::rel;
            root["host"]              = _opts.bindHost;
            root["port"]              = _opts.bindPort;
            root["password_required"] = !_opts.bindPassword.empty();
            root["max_players"]       = _opts.maxPlayers;
            root["mod_config"]        = Framework::Utils::ExtractReplicatedConfig(_opts.modConfigSchema, _modConfig);
            res.body                  = root.dump(4);
            res.status                = 200;
        });

        Logging::GetLogger(FRAMEWORK_INNER_HTTP)->debug("All core endpoints have been registered!");
    }


    bool Instance::LoadConfigFromJSON() {
        auto configHandle = cppfs::fs::open(_opts.modConfigFile);

        if (!configHandle.exists()) {
            // Write one instead of starting on invisible defaults. A generated file documents every
            // key the mod understands, which is the difference between a server operator being able
            // to see what is configurable and having to read the mod's source.
            const nlohmann::json defaults = BuildDefaultConfigFile();
            try {
                configHandle.writeFile(defaults.dump(4));
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Wrote a default '{}'", _opts.modConfigFile);
            }
            catch (const std::exception &ex) {
                // Not fatal: the resolved defaults below are the same either way, the operator just
                // does not get a file to edit.
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Could not write a default '{}': {}", _opts.modConfigFile, ex.what());
            }

            _modConfig = defaults.contains("mod") ? defaults["mod"] : nlohmann::json::object();
            std::string schemaError;
            if (!Framework::Utils::ValidateConfigAgainstSchema(_opts.modConfigSchema, _modConfig, schemaError)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Default config is invalid: {}", schemaError);
                return false;
            }
            return true;
        }

        const auto configData = configHandle.readFile();

        try {
            // Parse our config data first
            _fileConfig->Parse(configData);

            if (!_fileConfig->IsParsed()) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("JSON config load has failed: {}", _fileConfig->GetLastError());
                return false;
            }

            // Retrieve fields and overwrite InstanceOptions defaults
            _opts.bindHost      = _fileConfig->Get<std::string>("host");
            _opts.bindPort      = _fileConfig->Get<int>("port");
            _opts.bindMapName   = _fileConfig->Get<std::string>("map");
            _opts.maxPlayers    = _fileConfig->Get<int>("maxplayers");
            _opts.bindSecretKey = _fileConfig->Get<std::string>("server-token");

            // Mod-declared keys live under "mod" so they cannot collide with framework keys added
            // later, and so the replicated subset is a filter over one object rather than a
            // subtraction over the whole document.
            auto *document = _fileConfig->GetDocument();
            _modConfig     = (document && document->contains("mod")) ? (*document)["mod"] : nlohmann::json::object();

            std::string schemaError;
            if (!Framework::Utils::ValidateConfigAgainstSchema(_opts.modConfigSchema, _modConfig, schemaError)) {
                // Fail here rather than at the first connect: an operator sees the reason at boot.
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("{}: {}", _opts.modConfigFile, schemaError);
                return false;
            }

            // Unknown keys are kept, not rejected. Downgrading a mod must not brick a config file.
            for (auto it = _modConfig.begin(); it != _modConfig.end(); ++it) {
                bool declared = false;
                for (const auto &field : _opts.modConfigSchema) {
                    if (field.key == it.key()) {
                        declared = true;
                        break;
                    }
                }
                if (!declared) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("{}: 'mod.{}' is not a key this build understands; keeping it", _opts.modConfigFile, it.key());
                }
            }
        }
        catch (const std::exception &ex) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("JSON config has missing fields: {}", ex.what());
            return false;
        }
        return true;
    }

    nlohmann::json Instance::BuildDefaultConfigFile() const {
        nlohmann::json frameworkKeys;
        frameworkKeys["host"]         = _opts.bindHost;
        frameworkKeys["port"]         = _opts.bindPort;
        frameworkKeys["map"]          = _opts.bindMapName;
        frameworkKeys["maxplayers"]   = _opts.maxPlayers;
        frameworkKeys["server-token"] = _opts.bindSecretKey;
        return Framework::Utils::BuildDefaultConfigDocument(_opts.modConfigSchema, frameworkKeys);
    }

    void Instance::PublishSessionConfig() {
        const auto replicated = Framework::Utils::ExtractReplicatedConfig(_opts.modConfigSchema, _modConfig);
        _replicatedModConfig  = replicated.dump();

        auto *net = _networkingEngine ? _networkingEngine->GetNetworkServer() : nullptr;
        if (!net) {
            return;
        }

        // Carried by MafiaNet's session handshake, which completes before either side reports a
        // connection. A client therefore has this in hand the moment it sees
        // ID_CONNECTION_REQUEST_ACCEPTED, which is before the asset phase and before any client
        // script runs -- the whole point of putting it there rather than in an ordinary message.
        // A mod wanting a shape of its own replaces this from PostInit, which runs after Init.
        net->SetSessionConfig(_replicatedModConfig);

        if (!replicated.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Publishing {} replicated config key(s) to clients", replicated.size());
        }
    }

    void Instance::InitNetworkingMessages() {
        const auto net = _networkingEngine->GetNetworkServer();
        // Build gate: a mismatched token fails the challenge inside NetworkServer; the peer never
        // reaches the asset phase.
        if (_opts.verifyBuildToken) {
            net->SetBuildToken(Framework::Networking::NetworkPeer::BuildToken(_opts.gameName, _opts.gameVersion, Utils::Version::rel, _opts.modVersion));
        }
        else {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Build token verification DISABLED; accepting any client version");
            net->SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);
        }

        // Build verified -> send the resource list (carries the ReadyEvent id and tick rate).
        net->SetOnClientAuthenticatedCallback([this, net](MafiaNet::RakNetGUID guid) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Build verified for player guid {}, sending resource list", guid.g);

            Framework::Networking::RPC::ServerResources resources;
            resources.readyEventId = Framework::Networking::NetworkServer::ReadyEventId(guid);
            resources.tickRate     = _opts.worldConfig.tickInterval;
            if (_scriptingModule) {
                for (const auto &resource : _scriptingModule->GetClientResourceList()) {
                    resources.resources.push_back({resource.name, resource.version});
                }
            }
            net->SendRPC(resources, guid);

            // Travels with the resource list: a client that learns the ranges only when
            // someone speaks would misjudge the first words it hears.
            _voiceServer.SendSettingsTo(guid);
        });

        net->SetOnPlayerDisconnectCallback([this, net](MafiaNet::Packet *packet, Framework::Networking::DisconnectionReason reason, const std::string &) {
            const auto guid = packet->guid;
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Disconnecting peer {}, reason: {}", guid.g, static_cast<uint32_t>(reason));
            _armedSpawnBarrierGuids.erase(guid.g);
            _readyPlayerGuids.erase(guid.g);
            // Drop voice state unconditionally: a peer that never reached the ready barrier can
            // still have been registered in the router by a position push.
            _voiceServer.OnPlayerDisconnect(guid.g);

            // Player notification and avatar teardown run in ReplicationManager::OnClosedConnection,
            // which RakNet fires before this packet is delivered; here we just finalise the connection.
            net->GetPeer()->CloseConnection(guid, true);
        });

        net->SetOnConnectionReadyCallback([this, net](int eventId, MafiaNet::RakNetGUID guid) {
            if (eventId != Framework::Networking::NetworkServer::ReadyEventId(guid) || _armedSpawnBarrierGuids.erase(guid.g) == 0 || !_readyPlayerGuids.insert(guid.g).second) {
                return;
            }
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Client spawn ready for player guid {}", guid.g);
            OnPlayerReady(MafiaNet::ToPeerGuid(guid));
            net->GetReadyEvent()->SetEvent(eventId, true);
        });

        // Client announces itself after assets. Gated on authentication so an unverified peer can't
        // conjure an avatar by sending this directly.
        net->RegisterRPC<Framework::Networking::RPC::ClientIdentity>([this, net](const Framework::Networking::RPC::ClientIdentity &payload, MafiaNet::Packet *packet) {
            const auto guid = packet->guid;
            if (!net->IsAuthenticated(guid)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Ignoring identity from unauthenticated peer {}", guid.g);
                return;
            }

            auto *replication = net->GetReplicationManager();
            const auto peerGuid = MafiaNet::ToPeerGuid(guid);
            if (replication && (replication->GetConnectionByGUID(guid) || replication->GetViewer(peerGuid))) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Ignoring duplicate identity from {}", guid.g);
                return;
            }

            // Sanitize before retention: nickname length-capped, ids digit strings or dropped.
            auto identity = payload;
            if (identity.name.size() > 64) {
                identity.name.resize(64);
            }
            const auto digits = [](std::string &value, size_t max) {
                if (value.size() > max || value.find_first_not_of("0123456789") != std::string::npos) {
                    value.clear();
                }
            };
            digits(identity.steamId, 32);
            digits(identity.discordId, 32);
            digits(identity.hardwareId, 128);
            net->SetPeerIdentity(guid, identity);

            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} guid {} hwid {}", identity.name, guid.g, identity.hardwareId);

            // The game builds the avatar and registers it as this connection's viewer; hand it the
            // metadata so it spawns with the real nickname/slot.
            PlayerConnectionData data;
            data.guid        = peerGuid;
            data.playerIndex = guid.systemIndex;
            data.nickname    = identity.name;
            data.hardwareID  = identity.hardwareId;
            data.steamId     = identity.steamId;
            data.discordId   = identity.discordId;
            OnPlayerConnect(data);

            // Gate opens: this connection now starts receiving the replicated world.
            net->PushReplicationConnection(guid);

            const int eventId = Framework::Networking::NetworkServer::ReadyEventId(guid);
            net->GetReadyEvent()->SetEvent(eventId, false);
            net->GetReadyEvent()->AddToWaitList(eventId, guid);
            _armedSpawnBarrierGuids.insert(guid.g);
        });

        // Incoming chat from clients. Sender resolution + command parsing happen here; the mod
        // observes via the OnChatMessage / OnChatCommand overrides.
        net->RegisterRPC<Framework::Networking::RPC::ChatMessage>([this](const Framework::Networking::RPC::ChatMessage &payload, MafiaNet::Packet *packet) {
            if (payload.text.empty()) {
                return;
            }
            // Resolve the sender from its connection's viewer entity.
            auto *engine = GetNetworkingEngine();
            auto *server = engine ? engine->GetNetworkServer() : nullptr;
            auto *repl   = server ? server->GetReplicationManager() : nullptr;
            auto *sender = repl ? repl->GetViewer(MafiaNet::ToPeerGuid(packet->guid)) : nullptr;
            if (!sender) {
                return;
            }
            HandleIncomingChat(sender->GetNetworkID(), payload.text);
        });

        // Client -> server scripting events (the client's Game.emitServer). Resolve the sender from
        // its viewer entity, then hand off to OnClientEvent, which routes into the dedicated
        // client-event table (Events.onClient) — never the global bus a client could otherwise
        // collide with.
        net->RegisterRPC<Framework::Integrations::Shared::RPC::EmitScriptEvent>([this](const Framework::Integrations::Shared::RPC::EmitScriptEvent &payload, MafiaNet::Packet *packet) {
            const std::string name = payload.GetEventName();
            if (name.empty()) {
                return;
            }
            auto *engine = GetNetworkingEngine();
            auto *server = engine ? engine->GetNetworkServer() : nullptr;
            auto *repl   = server ? server->GetReplicationManager() : nullptr;
            auto *sender = repl ? repl->GetViewer(MafiaNet::ToPeerGuid(packet->guid)) : nullptr;
            if (!sender) {
                return;
            }
            OnClientEvent(sender->GetNetworkID(), name, payload.GetPayload());
        });

        // A client announcing whether its player left voice chat on. The GUID is the packet's,
        // never a field, so a peer can only ever speak for itself.
        net->RegisterRPC<Framework::Networking::RPC::VoicePreference>([this, net](const Framework::Networking::RPC::VoicePreference &payload, MafiaNet::Packet *packet) {
            if (!net->IsAuthenticated(packet->guid)) {
                return;
            }
            _voiceServer.OnPlayerPreference(static_cast<uint64_t>(MafiaNet::ToPeerGuid(packet->guid)), payload.enabled);
        });

        // Voice frames are not RPCs: RakVoice writes a raw message id, so they surface on the
        // unknown-packet path (the relay host deliberately declines to consume them itself).
        net->SetUnknownPacketHandler([this, net](MafiaNet::Packet *packet) {
            // GetPacketDataOffset() is the offset the peer resolved for this very packet, so an
            // ID_TIMESTAMP prefix is already skipped.
            const int offset = net->GetPacketDataOffset();
            if (offset < 0 || static_cast<uint32_t>(offset) >= packet->length) {
                return;
            }
            if (packet->data[offset] == ID_RAKVOICE_RELAY_DATA) {
                _voiceServer.OnVoiceFrame(packet);
            }
        });

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Networking messages registered");
    }

    void Instance::HandleIncomingChat(uint64_t senderNetworkId, const std::string &text) {
        if (text.empty()) {
            return;
        }
        if (text[0] != '/') {
            OnChatMessage(senderNetworkId, text);
            return;
        }
        // Same tokenizer as console commands, so a line parses identically on both surfaces.
        std::vector<std::string> tokens = Utils::CommandProcessor::Tokenize(std::string_view(text).substr(1));
        if (tokens.empty()) {
            return;
        }
        const std::string command = std::move(tokens.front());
        tokens.erase(tokens.begin());
        OnChatCommand(senderNetworkId, text, command, tokens);
    }

    void Instance::OnClientEvent(uint64_t senderNetworkId, const std::string &eventName, const std::string &payloadJson) {
        if (!_scriptingModule) {
            return;
        }
        auto *engine          = _scriptingModule->GetEngine();
        auto *resourceManager = _scriptingModule->GetResourceManager();
        if (!engine || !resourceManager || !engine->IsInitialized()) {
            return;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        std::vector<v8::Local<v8::Value>> args;
        args.push_back(WrapScriptPlayer(isolate, senderNetworkId));

        // The payload is untrusted. Parse it under a TryCatch: v8 rejects (and schedules an exception
        // for) inputs like too-deeply-nested JSON, so on any failure drop the whole event — continuing
        // into EmitClient with an exception pending on the isolate would abort the process.
        if (!payloadJson.empty()) {
            v8::TryCatch tryCatch(isolate);
            v8::Local<v8::String> jsonStr;
            v8::Local<v8::Value> parsed;
            if (!v8::String::NewFromUtf8(isolate, payloadJson.c_str()).ToLocal(&jsonStr) ||
                !v8::JSON::Parse(context, jsonStr).ToLocal(&parsed)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Dropping client event '{}' from {}: malformed JSON payload", eventName, senderNetworkId);
                return;
            }
            args.push_back(parsed);
        }

        resourceManager->GetEvents().EmitClient(isolate, context, eventName, args);
    }

    v8::Local<v8::Value> Instance::WrapScriptPlayer(v8::Isolate *isolate, uint64_t networkId) {
        Framework::Scripting::Builtins::Player::GetClass(isolate);
        return v8pp::class_<Framework::Scripting::Builtins::Player>::create_object(isolate, networkId);
    }

    void Instance::InitAssetStreamer() {
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Setting up asset streamer...");
        const auto net      = GetNetworkingEngine()->GetNetworkServer();
        const auto streamer = net->GetAssetStreamer();

        const auto scripting     = GetScriptingModule();
        const auto resourcesPath = scripting->GetResourcesPath();
        const std::string assetsPath = Framework::Utils::GetAbsolutePathA(resourcesPath);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Resources directory: {}", assetsPath);

        streamer->SetApplicationDirectory(assetsPath.c_str());

        // Add client files from each JS resource
        const auto resourceManager = scripting->GetResourceManager();
        if (resourceManager) {
            for (const auto &resourceName : resourceManager->GetAllResourceNames()) {
                const auto resource = resourceManager->GetResource(resourceName);
                if (!resource) continue;

                // Only process resources with client entry points
                const auto &clientEntryRelative = resource->GetManifest().GetMafiaHubConfig().client;
                if (clientEntryRelative.empty()) continue;

                const auto resourcePath = resource->GetPath();

                // Add package.json for client to parse manifest info
                std::filesystem::path packageJsonPath = std::filesystem::path(resourcePath) / "package.json";
                if (std::filesystem::exists(packageJsonPath)) {
                    std::filesystem::path packageJsonName = std::filesystem::path(resourceName) / "package.json";
                    streamer->AddFile(packageJsonPath.string().c_str(), packageJsonName.string().c_str());
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->trace("Added client asset: {}", packageJsonName.string());
                }

                // Add the client entry point script
                std::filesystem::path clientEntryPath = std::filesystem::path(resourcePath) / clientEntryRelative;
                if (std::filesystem::exists(clientEntryPath)) {
                    std::filesystem::path clientEntryName = std::filesystem::path(resourceName) / clientEntryRelative;
                    streamer->AddFile(clientEntryPath.string().c_str(), clientEntryName.string().c_str());
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->trace("Added client asset: {}", clientEntryName.string());
                }

                // If the client entry is in a subdirectory, add all script and web-view asset files
                // from that directory (pages served to views via http://resources/<resource>/<file>)
                static const std::set<std::string> kClientAssetExtensions = {".js", ".mjs", ".ts", ".json", ".html", ".htm", ".css", ".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".ico", ".ttf", ".otf", ".woff", ".woff2", ".patch"};
                std::filesystem::path clientDir = clientEntryPath.parent_path();
                if (clientDir != resourcePath && std::filesystem::exists(clientDir)) {
                    for (const auto &entry : std::filesystem::recursive_directory_iterator(clientDir)) {
                        if (!entry.is_regular_file()) continue;

                        const auto ext = entry.path().extension().string();
                        if (kClientAssetExtensions.contains(ext)) {
                            std::filesystem::path relativePath = std::filesystem::relative(entry.path(), std::filesystem::path(assetsPath));
                            streamer->AddFile(entry.path().string().c_str(), relativePath.string().c_str());
                            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->trace("Added client asset: {}", relativePath.string());
                        }
                    }
                }
            }
        }

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Asset streamer ready with {} client files", streamer->GetNumberOfFilesForUpload());
    }

    void Instance::BroadcastResourceRefresh(const std::string &name) {
        if (!_scriptingModule || !_networkingEngine) {
            return;
        }
        auto *rm = _scriptingModule->GetResourceManager();
        if (!rm) {
            return;
        }
        const auto *resource = rm->GetResource(name);
        if (!resource) {
            return;
        }
        // Only resources with a client entry point need a client-side refresh.
        if (resource->GetManifest().GetMafiaHubConfig().client.empty()) {
            return;
        }

        const auto net = _networkingEngine->GetNetworkServer();
        if (!net) {
            return;
        }

        // Rebuild the streamer's upload list so changed files get fresh hashes;
        // the delta transfer compares stored hashes. See docs/resource_hot_reload.md.
        if (auto *streamer = net->GetAssetStreamer()) {
            streamer->ClearUploads();
        }
        InitAssetStreamer();

        Framework::Networking::RPC::ResourceRefresh refresh;
        refresh.resources.push_back({resource->GetName(), resource->GetVersion()});
        net->BroadcastRPC(refresh);

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Broadcasting hot-reload of resource '{}' to clients", name);
    }

    void Instance::BroadcastResourceStop(const std::string &name) {
        if (!_scriptingModule || !_networkingEngine) {
            return;
        }
        auto *rm = _scriptingModule->GetResourceManager();
        if (!rm) {
            return;
        }
        const auto *resource = rm->GetResource(name);
        if (!resource) {
            return;
        }
        // Only client resources need a client-side stop.
        if (resource->GetManifest().GetMafiaHubConfig().client.empty()) {
            return;
        }

        const auto net = _networkingEngine->GetNetworkServer();
        if (!net) {
            return;
        }

        // No streamer rebuild: stopping ships no files.
        Framework::Networking::RPC::ResourceStop stop;
        stop.resources.push_back({resource->GetName(), resource->GetVersion()});
        net->BroadcastRPC(stop);

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Broadcasting stop of resource '{}' to clients", name);
    }

    void Instance::InitCommandListener() {
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Setting up command listener and processor...");
        
        _commandListener->SetCommandCallback([this](const std::string &command) {
            this->HandleCommand(command);
        });
        
        _commandProcessor->RegisterCommand(
            "help", {},
            [this](cxxopts::ParseResult &) {
                std::stringstream ss;
                for (const auto &name : _commandProcessor->GetCommandNames()) {
                    ss << fmt::format("{} {:>8}\n", name, _commandProcessor->GetCommandInfo(name)->options->help());
                }
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Available commands:\n{}", ss.str());
            },
            "Show this help message");
            
        _commandProcessor->RegisterCommand(
            "quit", {},
            [this](cxxopts::ParseResult &) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Stopping server...");
                Shutdown();
            },
            "Stop the server");

        // No argument: stop the server (back-compat). With one: stop a resource.
        _commandProcessor->RegisterCommand(
            "stop", {},
            [this](cxxopts::ParseResult &result) {
                const auto &args = result.unmatched();
                if (args.empty()) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Stopping server...");
                    Shutdown();
                    return;
                }
                auto *rm = _scriptingModule ? _scriptingModule->GetResourceManager() : nullptr;
                if (!rm) {
                    return;
                }
                auto res = rm->StopResource(args[0]);
                if (res) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Stopped resource '{}'", args[0]);
                } else {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to stop '{}': {}", args[0], res.GetError());
                }
            },
            "Stop a resource (stop <resource>), or the server if no resource is given");

        _commandProcessor->RegisterCommand(
            "status", {},
            [this](cxxopts::ParseResult &) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Server status:");
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("  Name: {}", _opts.modName);
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("  Host: {}:{}", _opts.bindHost, _opts.bindPort);
                
                if (_networkingEngine) {
                    const auto net = _networkingEngine->GetNetworkServer();
                    const auto peer = net->GetPeer();
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("  Players: {}/{}", peer->NumberOfConnections(), _opts.maxPlayers);
                }
            },
            "Show server status");

        // Resource lifecycle commands. Helper folds the shared boilerplate
        // (resolve manager, require a name, log the result).
        auto resourceCommand = [this](std::string_view verb, auto op) {
            return [this, verb, op](cxxopts::ParseResult &result) {
                auto *rm = _scriptingModule ? _scriptingModule->GetResourceManager() : nullptr;
                if (!rm) {
                    return;
                }
                const auto &args = result.unmatched();
                if (args.empty()) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Usage: {} <resource>", verb);
                    return;
                }
                auto res = op(rm, args[0]);
                if (res) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("{}: '{}'", verb, args[0]);
                } else {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to {} '{}': {}", verb, args[0], res.GetError());
                }
            };
        };

        _commandProcessor->RegisterCommand(
            "start", {},
            resourceCommand("start", [](Framework::Scripting::ResourceManager *rm, const std::string &n) {
                return rm->StartResource(n);
            }),
            "Start a resource: start <resource>");

        _commandProcessor->RegisterCommand(
            "restart", {},
            resourceCommand("restart", [](Framework::Scripting::ResourceManager *rm, const std::string &n) {
                if (!rm->IsResourceRunning(n)) {
                    return Framework::Scripting::ResourceOperationResult(std::string("resource is not running (use start)"));
                }
                return rm->RestartResource(n);
            }),
            "Reload a running resource's code: restart <resource>");

        // Start-or-reload — the canonical verb FiveM/MTASA operators expect.
        _commandProcessor->RegisterCommand(
            "ensure", {},
            resourceCommand("ensure", [](Framework::Scripting::ResourceManager *rm, const std::string &n) {
                return rm->IsResourceRunning(n) ? rm->RefreshResource(n) : rm->StartResource(n);
            }),
            "Start or reload a resource: ensure <resource>");

        // Re-scan for new/changed resources (manifests), without restarting.
        _commandProcessor->RegisterCommand(
            "refresh", {},
            [this](cxxopts::ParseResult &) {
                auto *rm = _scriptingModule ? _scriptingModule->GetResourceManager() : nullptr;
                if (!rm) {
                    return;
                }
                auto added = rm->Rescan();
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Refreshed resources ({} new)", added.size());
            },
            "Re-scan the resources directory for new/changed resources");

        _commandProcessor->RegisterCommand(
            "refreshall", {},
            [this](cxxopts::ParseResult &) {
                auto *rm = _scriptingModule ? _scriptingModule->GetResourceManager() : nullptr;
                if (!rm) {
                    return;
                }
                auto res = rm->RefreshAll();
                if (res) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Refreshed all resources ({} affected)", res.GetValue().size());
                } else {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to refresh all resources: {}", res.GetError());
                }
            },
            "Re-scan and reload all running resources from disk");

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Command listener and processor initialized");
    }

    void Instance::HandleCommand(std::string_view command) {
        try {
            auto result = _commandProcessor->ProcessCommand(command);
            if (result.GetError() != Utils::CommandProcessorError::COMMAND_NONE) {
                switch (result.GetError()) {
                    case Utils::CommandProcessorError::COMMAND_PRINT_HELP:
                        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("{}", result.GetValue());
                        break;
                    case Utils::CommandProcessorError::COMMAND_UNKNOWN: {
                        // Not a built-in command; hand it to the mod override and the scripting layer.
                        std::vector<std::string> tokens = Utils::CommandProcessor::Tokenize(command);
                        if (!tokens.empty()) {
                            const std::string name = std::move(tokens.front());
                            tokens.erase(tokens.begin());
                            OnConsoleCommand(std::string(command), name, tokens);
                            EmitConsoleCommand(name, tokens);
                        }
                        break;
                    }
                    case Utils::CommandProcessorError::COMMAND_EMPTY_INPUT:
                        break;
                    case Utils::CommandProcessorError::COMMAND_INTERNAL_ERROR:
                        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Error processing command ({}): {}", command, result.GetValue());
                        break;
                    default:
                        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Error processing command ({}): {}", command, static_cast<int>(result.GetError()));
                        break;
                }
            }
        }
        catch (const std::exception &ex) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Error processing command: {}", ex.what());
        }
    }

    void Instance::EmitConsoleCommand(const std::string &command, const std::vector<std::string> &args) {
        if (!_scriptingModule) {
            return;
        }
        auto *engine          = _scriptingModule->GetEngine();
        auto *resourceManager = _scriptingModule->GetResourceManager();
        if (!engine || !resourceManager || !engine->IsInitialized()) {
            return;
        }

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        v8::Local<v8::Array> argsArr = v8::Array::New(isolate, static_cast<int>(args.size()));
        for (size_t i = 0; i < args.size(); ++i) {
            argsArr->Set(context, static_cast<uint32_t>(i), v8pp::to_v8(isolate, args[i])).Check();
        }

        std::vector<v8::Local<v8::Value>> eventArgs;
        eventArgs.push_back(v8pp::to_v8(isolate, command));
        eventArgs.push_back(argsArr);
        resourceManager->GetEvents().EmitReserved(isolate, context, "consoleCommand", eventArgs);
    }

    void Instance::Shutdown() {
        if (_shuttingDown) {
            return;
        }

        _shuttingDown = true;

        PreShutdown();

        if (_scriptingModule) {
            _scriptingModule->PreShutdown();
        }

        // Detach from the peer before the networking engine tears it down.
        _voiceServer.Shutdown();

        if (_networkingEngine) {
            _networkingEngine->Shutdown();
        }

        if (_scriptingModule) {
            _scriptingModule->Shutdown();
        }

        if (_webServer) {
            _webServer->Shutdown();
        }

        if (_commandListener) {
            _commandListener->Shutdown();
        }

        // Drain, never close: the reporter outlives this instance.
        if (_crashReporter && _crashReporter->IsInitialized()) {
            _crashReporter->Flush();
        }

        // Detach signal handlers
        sig_detach(SIGINT, sig_slot(this, &Instance::OnSignal));
        sig_detach(SIGTERM, sig_slot(this, &Instance::OnSignal));

        CoreModules::SetNetworkPeer(nullptr);
        CoreModules::SetReplication(nullptr);
        CoreModules::SetVoiceServer(nullptr);
        CoreModules::SetScriptingModule(nullptr);
        CoreModules::Reset();

        Lifecycle::Shutdown();

        // Last: flush and tear down the async logging thread pool before static
        // destruction can race it.
        Logging::GetInstance()->Shutdown();
    }

    void Instance::Update() {
        const auto start = std::chrono::high_resolution_clock::now();
        if (_nextTick <= start) {
            FW_PROFILE_SCOPE_N("Server::Tick");

            if (_networkingEngine) {
                FW_PROFILE_SCOPE_N("Server::Networking");
                _networkingEngine->Update();
            }

            // Refresh the voice router's world view from the replicated entities. Every entity
            // carrying an owner GUID is a player-controlled one, which is exactly the set the
            // proximity rule keys on; ForEachEntity avoids the per-entity dynamic_cast that
            // ForEach<NetworkEntity> would cost for no added selectivity.
            if (auto *replication = _networkingEngine ? _networkingEngine->GetNetworkServer()->GetReplicationManager() : nullptr) {
                FW_PROFILE_SCOPE_N("Server::VoicePositions");
                auto &router = _voiceServer.GetRouter();
                replication->ForEachEntity([&router](Framework::Networking::Replication::NetworkEntity *entity) {
                    if (entity->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
                        router.SetPlayerPosition(static_cast<uint64_t>(entity->ownerGUID), entity->position);
                    }
                });
                _voiceServer.Update();
            }

            if (_scriptingModule) {
                FW_PROFILE_SCOPE_N("Server::Scripting");
                _scriptingModule->Update();
            }

            if (_commandListener) {
                FW_PROFILE_SCOPE_N("Server::Commands");
                _commandListener->Update();
            }

            if (_masterlist->IsInitialized()) {
                FW_PROFILE_SCOPE_N("Server::MasterlistPing");
                Services::ServerInfo info {};
                info.port           = _opts.bindPort;
                info.gameMode       = _opts.modName;
                info.version        = _opts.modVersion;
                info.maxPlayers     = _opts.maxPlayers;
                info.currentPlayers = _networkingEngine->GetNetworkServer()->GetPeer()->NumberOfConnections();
                _masterlist->Ping(info);
            }

            {
                FW_PROFILE_SCOPE_N("Server::PostUpdate");
                PostUpdate();
            }

            FW_PROFILE_FRAME();

            _nextTick = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(Utils::Time::SecondsToMs(_opts.worldConfig.tickInterval)));
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void Instance::Run() {
        while (_initialized) {
            Update();
            std::this_thread::yield();
        }
    }

    void Instance::OnSignal(const sig_signal_t signal) {
        if (!_initialized || _shuttingDown) {
            return;
        }

        if (signal.context != sig_ctx_sys()) {
            return;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Received shutdown signal. In progress...");

        Shutdown();
    }

    void Instance::RegisterScriptingBuiltins(Framework::Scripting::Engine *engine) {
        // JS bindings are registered by ServerScriptingModule::RegisterFrameworkBindings
        // This method is called to allow mod-specific customization
        ModuleRegister(engine);
    }

} // namespace Framework::Integrations::Server
