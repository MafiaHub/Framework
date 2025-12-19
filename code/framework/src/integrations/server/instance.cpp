/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include "core_modules.h"
#include "world/server.h"

#include "networking/messages/client_connection_finalized.h"
#include "networking/messages/client_handshake.h"
#include "networking/messages/client_initialise_player.h"
#include "networking/messages/client_kick.h"
#include "networking/messages/client_ready_assets.h"
#include "networking/messages/client_request_streamer.h"
#include "networking/messages/messages.h"
#include "integrations/shared/rpc/emit_lua_event.h"

#include "scripting/builtins/entity.h"
#include "scripting/builtins/events_lua.h"
#include "scripting/utils/table_conversions.h"

#include "utils/command_processor.h"
#include "utils/path.h"
#include "utils/version.h"
#include "../shared/modules/mod.hpp"

#include "cxxopts.hpp"
#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <csignal>

namespace Framework::Integrations::Server {
    Instance::Instance(): _alive(false), _shuttingDown(false) {
        _networkingEngine = std::make_shared<Networking::Engine>();
        _webServer        = std::make_shared<HTTP::Webserver>();
        _fileConfig       = std::make_unique<Utils::Config>();
        _worldEngine      = std::make_shared<World::ServerEngine>();
        _scriptingModule  = std::make_shared<Scripting::ServerScriptingModule>(_worldEngine);
        _playerFactory    = std::make_shared<World::Archetypes::PlayerFactory>();
        _streamingFactory = std::make_shared<World::Archetypes::StreamingFactory>();
        _masterlist       = std::make_unique<Services::MasterlistConnector>();
        _commandListener  = std::make_shared<Utils::CommandListener>();
        _commandProcessor = std::make_shared<Utils::CommandProcessor>();
    }

    Instance::~Instance() {
        sig_detach(this);
    }

    ServerError Instance::Init(InstanceOptions &opts) {
        _opts = opts;

        if (opts.gameName.empty() || opts.gameVersion.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Game name and version are required");
            return ServerError::SERVER_INVALID_OPTIONS;
        }

        CoreModules::SetTickRate(opts.tickInterval);

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
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to parse JSON config file");
            return ServerError::SERVER_CONFIG_PARSE_ERROR;
        }

        // Finally apply back to the structure that is used everywhere the settings from the parser
        _opts.bindHost = result["host"].as<std::string>();
        _opts.bindPort = result["port"].as<int32_t>();

        // Initialize the logging instance with the mod slug name
        Logging::GetInstance()->SetLogName(_opts.modSlug);

        // Initialize the web server
        if (_opts.webServerEnabled && !_webServer->Init(_opts.webBindHost, _opts.webBindPort, _opts.httpServeDir)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the webserver engine");
            return ServerError::SERVER_WEBSERVER_INIT_FAILED;
        }

        // Initialize our networking engine
        if (!_networkingEngine->Init(_opts.bindPort, _opts.bindHost, _opts.maxPlayers, _opts.bindPassword)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the networking engine");
            return ServerError::SERVER_NETWORKING_INIT_FAILED;
        }

        // Initialize the world
        if (_worldEngine->Init(_networkingEngine->GetNetworkServer(), _opts.streamerTickInterval) != World::EngineError::ENGINE_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the world engine");
            return ServerError::SERVER_WORLD_INIT_FAILED;
        }

        if (_opts.bindPublicServer && !_masterlist->Init(_opts.services.apiUrl, _opts.services.masterlistUrl, _opts.bindSecretKey)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Server will not be announced to masterlist");
        }

        // Init the signals handlers if enabled
        if (_opts.enableSignals) {
            sig_attach(SIGINT, sig_slot(this, &Instance::OnSignal), sig_ctx_sys());
            sig_attach(SIGTERM, sig_slot(this, &Instance::OnSignal), sig_ctx_sys());
        }

        // Register the default endpoints
        InitEndpoints();

        // Register built in modules
        InitModules();

        // Initialize default messages
        InitNetworkingMessages();

        // Initialize command listener
        InitCommandListener();

        // Initialize mod subsystems
        PostInit();
    
        const auto sdkCallback = [this](Framework::Scripting::SDKRegisterWrapper<Framework::Scripting::Engine> sdk) {
            this->RegisterScriptingBuiltins(sdk.GetEngine());
        };

        // Initialize the scripting engine
        _scriptingModule->SetMainGamemodePath("gamemode");
        _scriptingModule->LoadManifest();
        if (!_scriptingModule->Init(sdkCallback)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the scripting engine");
            return ServerError::SERVER_SCRIPTING_INIT_FAILED;
        }

        PostScriptInit();

        // Initialize asset streamer
        InitAssetStreamer();

        // Load the gamemode
        _scriptingModule->GetEngine()->LoadScript();

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->flush();
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Host:\t{}", _opts.bindHost);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Port:\t{}", _opts.bindPort);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Max Players:\t{}", _opts.maxPlayers);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("{} Server successfully started", _opts.modName);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->flush();

        _alive        = true;
        _shuttingDown = false;
        return ServerError::SERVER_NONE;
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

    void Instance::InitModules() const {
        if (_worldEngine) {
            const auto world = _worldEngine->GetWorld();

            world->import <Integrations::Shared::Modules::Mod>();
        }

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Core ecs modules have been imported!");
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

    void Instance::InitNetworkingMessages() const {
        using namespace Framework::Networking::Messages;
        const auto net = _networkingEngine->GetNetworkServer();
        net->RegisterMessage<ClientHandshake>(Framework::Networking::Messages::GameMessages::GAME_CONNECTION_HANDSHAKE, [this, net](SLNet::RakNetGUID guid, ClientHandshake *msg) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Received handshake message for incoming player guid {}", guid.g);

            // Make sure handshake payload was correctly formatted
            if (!msg->Valid()) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Handshake payload was invalid, force-disconnecting peer");
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            if (msg->GetGameName() != _opts.gameName) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Client has invalid game, force-disconnecting peer");
                Framework::Networking::Messages::ClientKick kick;
                kick.FromParameters(Framework::Networking::Messages::DisconnectionReason::WRONG_VERSION);
                net->Send(kick, guid);
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            const auto fwVersion = msg->GetFWVersion();

            if (!Utils::Version::VersionSatisfies(fwVersion.c_str(), Utils::Version::rel)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Client has invalid Framework version, force-disconnecting peer");
                Framework::Networking::Messages::ClientKick kick;
                kick.FromParameters(Framework::Networking::Messages::DisconnectionReason::WRONG_VERSION);
                net->Send(kick, guid);
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            const auto clientVersion = msg->GetClientVersion();

            if (!Utils::Version::VersionSatisfies(clientVersion.c_str(), _opts.modVersion.c_str())) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Client has invalid version, force-disconnecting peer");
                Framework::Networking::Messages::ClientKick kick;
                kick.FromParameters(Framework::Networking::Messages::DisconnectionReason::WRONG_VERSION);
                net->Send(kick, guid);
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            const auto mpClientVersion = msg->GetGameVersion();

            if (mpClientVersion != _opts.gameVersion) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Client has invalid game version, force-disconnecting peer");
                Framework::Networking::Messages::ClientKick kick;
                kick.FromParameters(Framework::Networking::Messages::DisconnectionReason::WRONG_VERSION);
                net->Send(kick, guid);
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            // Let the client know they can ask for client-side assets now.
            // Include the resource list in the message.
            ClientReadyAssets readyMsg;
            if (_scriptingModule) {
                for (const auto &resource : _scriptingModule->GetClientResourceList()) {
                    readyMsg.AddResource(resource.name, resource.version, resource.hash);
                }
            }
            net->Send(readyMsg, guid);
        });

        net->SetOnPlayerDisconnectCallback([this, net](SLNet::Packet *packet, uint32_t reason) {
            const auto guid = packet->guid;
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Disconnecting peer {}, reason: {}", guid.g, reason);

            const auto e = _worldEngine->GetEntityByGUID(guid.g);
            if (e.is_valid()) {
                if (_onPlayerDisconnectCallback)
                    _onPlayerDisconnectCallback(e, guid.g);

                _worldEngine->RemoveEntity(e);
            }

            net->GetPeer()->CloseConnection(guid, true);
        });

        
        net->RegisterMessage<ClientRequestStreamer>(GameMessages::GAME_CONNECTION_REQUEST_STREAMER, [this, net](SLNet::RakNetGUID guid, ClientRequestStreamer *msg) {
            // Create player entity and add on world
            const auto newPlayer = _worldEngine->CreateEntity();
            _streamingFactory->SetupServer(newPlayer, guid.g);

            auto nickname = msg->GetPlayerName();
            if (nickname.size() > 64) {
                nickname = nickname.substr(0, 64);
            }

            _playerFactory->SetupServer(newPlayer, guid.g, guid.systemIndex, nickname);

            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} guid {} entity id {}", msg->GetPlayerName(), guid.g, newPlayer.id());

            // Send the connection finalized packet
            Framework::Networking::Messages::ClientConnectionFinalized answer;
            answer.FromParameters(_opts.tickInterval, newPlayer.id());
            net->Send(answer, guid);
        });

        net->RegisterMessage<ClientInitPlayer>(Framework::Networking::Messages::GameMessages::GAME_INIT_PLAYER, [this, net](SLNet::RakNetGUID guid, ClientInitPlayer *stub) {
            const auto e = _worldEngine->GetEntityByGUID(guid.g);
            if (_onPlayerConnectCallback && e.is_valid() && e.is_alive())
                _onPlayerConnectCallback(e, guid.g);
        });

        net->RegisterRPC<Shared::RPC::EmitLuaEvent>([this](SLNet::RakNetGUID guid, Shared::RPC::EmitLuaEvent *rpc) {
            if (!rpc->Valid())
                return;
            
            const auto eventName = rpc->GetEventName();
            const auto payloadStr = rpc->GetPayload();
            sol::object payload {};
            try {
                nlohmann::json payloadJson = nlohmann::json::parse(payloadStr);
                payload                    = Framework::Scripting::Utils::JsonToSol(sol::this_state(_scriptingModule->GetEngine()->GetLuaEngine()->lua_state()), payloadJson);
            }
            catch (const std::exception &ex) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to parse event payload: {}", ex.what());
                return;
            }
            _scriptingModule->GetEngine()->InvokeRemoteEvent(eventName, payload);
        });

        Framework::World::Modules::Base::SetupServerReceivers(net, _worldEngine.get());

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Game sync networking messages registered");
    }

    void Instance::InitAssetStreamer() {
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Setting up asset streamer...");
        const auto net      = GetNetworkingEngine()->GetNetworkServer();
        const auto streamer = net->GetAssetStreamer();

        const auto scripting   = GetScriptingModule();
        const auto gamemodePath = scripting->GetEngine()->GetMainGamemodePath();
        const auto clientPath = fmt::format("{}\\client", gamemodePath);
        const auto clientFiles       = scripting->GetClientFiles();
        const std::string assetsPath = Framework::Utils::GetAbsolutePathA(clientPath);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Client assets directory: {}", assetsPath);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->flush();

        streamer->SetApplicationDirectory(assetsPath.c_str());
        
        for (const auto& fileName : clientFiles) {
            streamer->AddFile(fmt::format("{}\\{}", assetsPath, fileName).c_str(), fileName.c_str());
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Added client asset: {}", fileName);
        }
    }

    void Instance::InitCommandListener() {
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Setting up command listener and processor...");
        
        _commandListener->SetCommandHandler([this](const std::string &command) {
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
                PreShutdown();
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
        
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Command listener and processor initialized");
    }

    void Instance::HandleCommand(const std::string &command) {
        try {
            auto result = _commandProcessor->ProcessCommand(command);
            if (result.GetError() != Utils::CommandProcessorError::ERROR_NONE) {
                switch (result.GetError()) {
                    case Utils::CommandProcessorError::ERROR_NONE_PRINT_HELP:
                        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("{}", result.Unwrap());
                        break;
                    case Utils::CommandProcessorError::ERROR_CMD_UNKNOWN:
                        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Unknown command ({}): {}", command, result.Unwrap());
                        break;
                    case Utils::CommandProcessorError::ERROR_EMPTY_INPUT:
                        break;
                    case Utils::CommandProcessorError::ERROR_INTERNAL:
                        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Error processing command ({}): {}", command, result.Unwrap());
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

    ServerError Instance::Shutdown() {
        if (_shuttingDown) {
            return ServerError::SERVER_NONE;
        }

        _shuttingDown = true;

        if (_networkingEngine) {
            _networkingEngine->Shutdown();
        }

        if (_scriptingModule) {
            _scriptingModule->Shutdown();
        }

        if (_webServer) {
            _webServer->Shutdown();
        }

        if (_worldEngine) {
            _worldEngine->Shutdown();
        }

        if (_commandListener) {
            _commandListener->Shutdown();
        }

        // Detach signal handlers
        sig_detach(SIGINT, sig_slot(this, &Instance::OnSignal));
        sig_detach(SIGTERM, sig_slot(this, &Instance::OnSignal));

        CoreModules::Reset();

        _alive = false;
        return ServerError::SERVER_NONE;
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

            if (_worldEngine) {
                _worldEngine->Update();
            }
            
            if (_commandListener) {
                _commandListener->Update();
            }

            if (_masterlist->IsInitialized()) {
                Services::ServerInfo info {};
                info.port           = _opts.bindPort;
                info.gameMode       = _scriptingModule->GetEngine()->GetGamemodeName();
                info.version        = Utils::Version::rel;
                info.maxPlayers     = _opts.maxPlayers;
                info.currentPlayers = _networkingEngine->GetNetworkServer()->GetPeer()->NumberOfConnections();
                _masterlist->Ping(info);
            }

            PostUpdate();

            _nextTick = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(_opts.tickInterval * 1000.0f));
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void Instance::Run() {
        while (_alive) {
            Update();
            std::this_thread::yield();
        }
    }

    void Instance::OnSignal(const sig_signal_t signal) {
        if (!_alive || _shuttingDown) {
            return;
        }

        if (signal.context != sig_ctx_sys()) {
            return;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Received shutdown signal. In progress...");

        PreShutdown();
        Shutdown();
    }

    void Instance::RegisterScriptingBuiltins(Framework::Scripting::Engine *engine) {
        // Register the entity builtin
        Framework::Integrations::Scripting::Entity::Register(engine->GetLuaEngine());
        Framework::Integrations::Scripting::EventsServer::Register(engine->GetLuaEngine());

        // mod-specific builtins
        ModuleRegister(engine);
    }

    void Instance::PreShutdown() {
        if (_scriptingModule) {
            _scriptingModule->PreShutdown();
        }
    }

} // namespace Framework::Integrations::Server
