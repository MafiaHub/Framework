/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core_modules.h"

#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "networking/rpc/chat_message.h"
#include "networking/rpc/client_identity.h"
#include "networking/rpc/resource_refresh.h"
#include "networking/rpc/server_resources.h"

#include "networking/connection.h"

#include "utils/command_processor.h"
#include "utils/path.h"
#include "utils/version.h"

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
    }

    Instance::~Instance() {
        sig_detach(this);
    }

    Utils::Result<void, Error> Instance::Init(InstanceOptions &opts) {
        _opts = opts;

        if (opts.gameName.empty() || opts.gameVersion.empty()) {
            return Error("Game name and version are required");
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

        // The networked world is the replication manager owned by the peer. Serialize entity updates
        // at the configured tick rate (tickInterval is in seconds).
        auto *replication = _networkingEngine->GetNetworkServer()->GetReplicationManager();
        CoreModules::SetReplication(replication);
        if (replication) {
            replication->SetAutoSerializeInterval(static_cast<MafiaNet::Time>(_opts.worldConfig.tickInterval * 1000.0f));
            replication->ConfigureGrid(_opts.worldConfig.streamCellSize, _opts.worldConfig.streamWorldMin, _opts.worldConfig.streamWorldMax);
            // Replication owns connection teardown: when a peer drops, it notifies the game (avatar
            // still resolvable) just before destroying and broadcasting the destruction of the avatar.
            replication->SetOnClientDisconnect([this](MafiaNet::PeerGuid guid) {
                OnPlayerDisconnect(guid);
            });
        }

        if (!_opts.bindPublicServer || !_masterlist->Init(_opts.services.apiUrl, _opts.services.masterlistUrl, _opts.bindSecretKey)) {
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

        // Notify connected clients when a client resource is hot-reloaded so
        // they can re-sync its files and restart it (dev mode).
        _scriptingModule->GetResourceManager()->SetOnResourceReloaded([this](const std::string &name) {
            BroadcastResourceRefresh(name);
        });

        // Start all resources (ES modules load asynchronously via normal Update cycle)
        auto startResult = _scriptingModule->GetResourceManager()->StartAll();
        if (!startResult) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to start resources: {}", startResult.GetError());
        }

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
            root["mod_version"]       = Utils::Version::rel;
            root["host"]              = _opts.bindHost;
            root["port"]              = _opts.bindPort;
            root["password_required"] = !_opts.bindPassword.empty();
            root["max_players"]       = _opts.maxPlayers;
            res.body                  = root.dump(4);
            res.status                = 200;
        });

        Logging::GetLogger(FRAMEWORK_INNER_HTTP)->debug("All core endpoints have been registered!");
    }


    bool Instance::LoadConfigFromJSON() {
        const auto configHandle = cppfs::fs::open(_opts.modConfigFile);

        if (!configHandle.exists()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("JSON config file is not present, skipping load...");
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
        }
        catch (const std::exception &ex) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("JSON config has missing fields: {}", ex.what());
            return false;
        }
        return true;
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
        });

        net->SetOnPlayerDisconnectCallback([net](MafiaNet::Packet *packet, Framework::Networking::DisconnectionReason reason, const std::string &) {
            const auto guid = packet->guid;
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Disconnecting peer {}, reason: {}", guid.g, static_cast<uint32_t>(reason));

            // Player notification and avatar teardown run in ReplicationManager::OnClosedConnection,
            // which RakNet fires before this packet is delivered; here we just finalise the connection.
            net->GetPeer()->CloseConnection(guid, true);
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

            auto nickname = payload.name;
            if (nickname.size() > 64) {
                nickname = nickname.substr(0, 64);
            }

            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} guid {} hwid {}", nickname, guid.g, payload.hardwareId);

            // The game builds the avatar and registers it as this connection's viewer; hand it the
            // metadata so it spawns with the real nickname/slot.
            PlayerConnectionData data;
            data.guid        = peerGuid;
            data.playerIndex = guid.systemIndex;
            data.nickname    = nickname;
            data.hardwareID  = payload.hardwareId;
            OnPlayerConnect(data);

            // Gate opens: this connection now starts receiving the replicated world.
            net->PushReplicationConnection(guid);

            // Arm our half of the spawn barrier (the client armed its half before sending identity).
            const int eventId = Framework::Networking::NetworkServer::ReadyEventId(guid);
            net->GetReadyEvent()->AddToWaitList(eventId, guid);
            net->GetReadyEvent()->SetEvent(eventId, true);
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

        // Note: Client-to-server events are handled through the JS Events system
        // The client can emit events via Framework.events.emitToServer() which uses
        // the networking messages system to send events to the server

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

                // If the client entry is in a subdirectory, add all JS files from that directory
                std::filesystem::path clientDir = clientEntryPath.parent_path();
                if (clientDir != resourcePath && std::filesystem::exists(clientDir)) {
                    for (const auto &entry : std::filesystem::recursive_directory_iterator(clientDir)) {
                        if (!entry.is_regular_file()) continue;

                        // Add JavaScript and TypeScript files
                        const auto ext = entry.path().extension().string();
                        if (ext == ".js" || ext == ".mjs" || ext == ".ts" || ext == ".json") {
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

        // Rebuild the asset streamer's upload list so the changed files get
        // fresh hashes. The delta transfer compares stored hashes, so without
        // this the edited file would look up-to-date and never be re-sent.
        if (auto *streamer = net->GetAssetStreamer()) {
            streamer->ClearUploads();
        }
        InitAssetStreamer();

        Framework::Networking::RPC::ResourceRefresh refresh;
        refresh.resources.push_back({resource->GetName(), resource->GetVersion()});
        net->BroadcastRPC(refresh);

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Broadcasting hot-reload of resource '{}' to clients", name);
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
            "stop", {},
            [this](cxxopts::ParseResult &) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Stopping server...");
                Shutdown();
            },
            "Stop the server");

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

        _commandProcessor->RegisterCommand(
            "refresh", {},
            [this](cxxopts::ParseResult &result) {
                auto *rm = _scriptingModule ? _scriptingModule->GetResourceManager() : nullptr;
                if (!rm) {
                    return;
                }
                const auto &args = result.unmatched();
                if (args.empty()) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Usage: refresh <resource>");
                    return;
                }
                auto res = rm->RefreshResource(args[0]);
                if (res) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Refreshed resource '{}'", args[0]);
                } else {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to refresh '{}': {}", args[0], res.GetError());
                }
            },
            "Reload a resource from disk: refresh <resource>");

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
            "Re-scan and reload all resources from disk");

        // FiveM-style start-or-reload: reload if running, start if stopped. The
        // canonical reload verb operators coming from FiveM/MTASA expect.
        _commandProcessor->RegisterCommand(
            "ensure", {},
            [this](cxxopts::ParseResult &result) {
                auto *rm = _scriptingModule ? _scriptingModule->GetResourceManager() : nullptr;
                if (!rm) {
                    return;
                }
                const auto &args = result.unmatched();
                if (args.empty()) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Usage: ensure <resource>");
                    return;
                }
                auto res = rm->IsResourceRunning(args[0]) ? rm->RefreshResource(args[0]) : rm->StartResource(args[0]);
                if (res) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Ensured resource '{}'", args[0]);
                } else {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to ensure '{}': {}", args[0], res.GetError());
                }
            },
            "Start or reload a resource: ensure <resource>");

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
                    case Utils::CommandProcessorError::COMMAND_UNKNOWN:
                        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Unknown command ({}): {}", command, result.GetValue());
                        break;
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

    void Instance::Shutdown() {
        if (_shuttingDown) {
            return;
        }

        _shuttingDown = true;

        PreShutdown();

        if (_scriptingModule) {
            _scriptingModule->PreShutdown();
        }

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

        // Detach signal handlers
        sig_detach(SIGINT, sig_slot(this, &Instance::OnSignal));
        sig_detach(SIGTERM, sig_slot(this, &Instance::OnSignal));

        CoreModules::SetNetworkPeer(nullptr);
        CoreModules::SetReplication(nullptr);
        CoreModules::SetScriptingModule(nullptr);
        CoreModules::Reset();

        Lifecycle::Shutdown();
    }

    void Instance::Update() {
        const auto start = std::chrono::high_resolution_clock::now();
        if (_nextTick <= start) {
            if (_networkingEngine) {
                _networkingEngine->Update();
            }

            if (_scriptingModule) {
                _scriptingModule->Update();
            }

            if (_commandListener) {
                _commandListener->Update();
            }

            if (_masterlist->IsInitialized()) {
                Services::ServerInfo info {};
                info.port           = _opts.bindPort;
                info.gameMode       = _opts.modName;
                info.version        = Utils::Version::rel;
                info.maxPlayers     = _opts.maxPlayers;
                info.currentPlayers = _networkingEngine->GetNetworkServer()->GetPeer()->NumberOfConnections();
                _masterlist->Ping(info);
            }

            PostUpdate();

            _nextTick = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(_opts.worldConfig.tickInterval * 1000.0f));
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
