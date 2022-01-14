/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

#include <logging/logger.h>

namespace Framework::Integrations::Server::Networking {
    Engine::Engine() {
        _networkServer = new Framework::Networking::NetworkServer;
    }

    bool Engine::Init(int32_t port, std::string &host, int32_t maxPlayers, std::string &password) {
        if (_networkServer->Init(port, host, maxPlayers, password) != Framework::Networking::SERVER_NONE) {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_SERVER)->critical("Failed to init the inner networking engine");
            return false;
        }

        return true;
    }

    bool Engine::Shutdown() {
        if (_networkServer) {
            _networkServer->Shutdown();
        }
        return true;
    }

    void Engine::Update() {
        if (_networkServer) {
            _networkServer->Update();
        }
    }
} // namespace Framework::Integrations::Server::Networking
