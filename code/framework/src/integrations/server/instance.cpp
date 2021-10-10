/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include <nlohmann/json.hpp>
#include <optick.h>

namespace Framework::Integrations::Server {
    Instance::Instance(): _alive(false) {
        _scriptingEngine  = new Scripting::Engine;
        _networkingEngine = new Networking::Engine;
        _webServer        = new HTTP::Webserver;
    }

    Instance::~Instance() {
        sig_detach(this);
    }

    ServerError Instance::Init(InstanceOptions &opts) {
        _opts = opts;

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

        // Register the default endpoints
        InitEndpoints();

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
        Shutdown();
    }
} // namespace Framework::Integrations::Server
