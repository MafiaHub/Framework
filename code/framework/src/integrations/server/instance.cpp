/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include "../shared/modules/mod.hpp"
#include "../shared/modules/server.hpp"
#include "../shared/types/environment.hpp"
#include "integrations/shared/messages/weather_update.h"

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#include <optick.h>

namespace Framework::Integrations::Server {
    Instance::Instance(): _alive(false) {
        _scriptingEngine  = std::make_unique<Scripting::Engine>();
        _networkingEngine = std::make_unique<Networking::Engine>();
        _webServer        = std::make_unique<HTTP::Webserver>();
        _worldEngine      = std::make_unique<World::Engine>();
        _firebaseWrapper  = std::make_unique<External::Firebase::Wrapper>();
        _masterlistSync   = std::make_unique<Masterlist>(this);
        _fileConfig       = std::make_unique<Utils::Config>();
    }

    Instance::~Instance() {
        sig_detach(this);
    }

    ServerError Instance::Init(InstanceOptions &opts) {
        if (!LoadConfigFromJSON()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to parse JSON config file");
            return ServerError::SERVER_CONFIG_PARSE_ERROR;
        }

        // First level is argument parser, because we might want to overwrite stuffs
        cxxopts::Options options(opts.modSlug, opts.modHelpText);
        options.add_options("", {{"p,port", "Networking port to bind", cxxopts::value<int32_t>()->default_value(std::to_string(opts.bindPort))},
                                    {"h,host", "Networking host to bind", cxxopts::value<std::string>()->default_value(opts.bindHost)}});

        // Try to parse and return if anything wrong happened
        auto result = options.parse(opts.argc, opts.argv);

        // Finally apply back to the structure that is used everywhere the settings from the parser
        opts.bindHost = result["host"].as<std::string>();
        opts.bindPort = result["port"].as<int32_t>();
        _opts         = opts;

        // Initialize the logging instance with the mod slug name
        Logging::GetInstance()->SetLogName(opts.modSlug);

        // Initialize the web server
        if (!_webServer->Init(opts.bindHost, opts.bindPort, opts.httpServeDir)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the webserver engine");
            return ServerError::SERVER_WEBSERVER_INIT_FAILED;
        }

        // Initialize the scripting engine
        if (_scriptingEngine->Init() != Scripting::EngineError::ENGINE_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the scripting engine");
            return ServerError::SERVER_SCRIPTING_INIT_FAILED;
        }

        // Initialize our networking engine
        if (!_networkingEngine->Init(opts.bindPort, opts.bindHost, opts.maxPlayers, opts.bindPassword)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the networking engine");
            return ServerError::SERVER_NETWORKING_INIT_FAILED;
        }

        // Initialize the world
        if (_worldEngine->Init() != World::EngineError::ENGINE_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the world engine");
            return ServerError::SERVER_WORLD_INIT_FAILED;
        }

        if (_opts.firebaseEnabled
            && _firebaseWrapper->Init(_opts.firebaseProjectId, _opts.firebaseAppId, _opts.firebaseApiKey) != External::Firebase::FirebaseError::FIREBASE_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the firebase wrapper");
            return ServerError::SERVER_FIREBASE_WRAPPER_INIT_FAILED;
        }

        // Register the default endpoints
        InitEndpoints();

        // Register built in modules
        InitModules();

        // Initialize built in managers
        InitManagers();

        // Initialize mod subsystems
        PostInit();

        // Init the signals handlers if enabled
        if (opts.enableSignals) {
            sig_attach(SIGINT, sig_slot(this, &Instance::OnSignal), sig_ctx_sys());
            sig_attach(SIGTERM, sig_slot(this, &Instance::OnSignal), sig_ctx_sys());
        }

        _alive = true;
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("{} Server successfully started", opts.modName);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Name:\t{}", opts.bindName);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Host:\t{}", opts.bindHost);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Port:\t{}", opts.bindPort);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Max Players:\t{}", opts.maxPlayers);
        return ServerError::SERVER_NONE;
    }

    void Instance::InitEndpoints() {
        _webServer->RegisterRequest("/networking/status", [this](struct mg_connection *c, void *ev_data, Framework::HTTP::ResponseCallback cb) {
            nlohmann::json root;
            root["mod_name"]          = _opts.modName;
            root["mod_slug"]          = _opts.modSlug;
            root["mod_version"]       = _opts.modVersion;
            root["host"]              = _opts.bindHost;
            root["port"]              = _opts.bindPort;
            root["password_required"] = !_opts.bindPassword.empty();
            root["max_players"]       = _opts.maxPlayers;
            cb(200, root.dump(4));
        });
    }

    void Instance::InitModules() {
        if (_worldEngine) {
            _worldEngine->GetWorld()->import<Integrations::Shared::Modules::Mod>();
            _worldEngine->GetWorld()->import<Integrations::Shared::Modules::Server>();
        }
    }

    void Instance::InitManagers() {
        // weather
        auto envFactory = Integrations::Shared::Archetypes::EnvironmentFactory(_worldEngine->GetWorld(), _networkingEngine.get());
        _weatherManager = envFactory.CreateWeather("WeatherManager");
    }

    bool Instance::LoadConfigFromJSON() {
        auto configHandle = cppfs::fs::open(_opts.modConfigFile);

        if (!configHandle.exists()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("JSON config file is not present, skipping load...");
            return true;
        }

        auto configData = configHandle.readFile();

        try {
            // Parse our config data first
            _fileConfig->Parse(configData);

            if (!_fileConfig->IsParsed()) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("JSON config load has failed: {}", _fileConfig->GetLastError());
                return false;
            }

            // Retrieve fields and overwrite InstanceOptions defaults
            _opts.bindHost      = _fileConfig->Get<std::string>("host");
            _opts.bindName      = _fileConfig->Get<std::string>("name");
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

    ServerError Instance::Shutdown() {
        if (_networkingEngine) {
            _networkingEngine->Shutdown();
        }

        if (_scriptingEngine) {
            _scriptingEngine->Shutdown();
        }

        if (_webServer) {
            _webServer->Shutdown();
        }

        if (_worldEngine) {
            _worldEngine->Shutdown();
        }

        // Detach signal handlers
        sig_detach(SIGINT, sig_slot(this, &Instance::OnSignal));
        sig_detach(SIGTERM, sig_slot(this, &Instance::OnSignal));

        _alive = false;
        return ServerError::SERVER_NONE;
    }

    void Instance::Update() {
        const auto start = std::chrono::high_resolution_clock::now();
        if (_nextTick <= start) {
            if (_networkingEngine) {
                _networkingEngine->Update();
            }

            if (_scriptingEngine) {
                _scriptingEngine->Update();
            }

            if (_worldEngine) {
                _worldEngine->Update();
            }

            if (_firebaseWrapper && _opts.firebaseEnabled && _opts.bindPublicServer) {
                _masterlistSync->Update(_firebaseWrapper.get());
            }

            PostUpdate();

            _nextTick = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(_opts.tickInterval);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void Instance::Run() {
        while (_alive) {
            OPTICK_FRAME("MainThread");
            Update();
        }
    }

    void Instance::OnSignal(const sig_signal_t signal) {
        if (signal.context != sig_ctx_sys()) {
            return;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Received shutdown signal. In progress...");

        PreShutdown();
        Shutdown();
    }
} // namespace Framework::Integrations::Server
