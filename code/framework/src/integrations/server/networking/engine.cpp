/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

#include <logging/logger.h>

namespace Framework::Integrations::Server::Networking {
    Engine::Engine() {
        _networkServer = std::make_unique<Framework::Networking::NetworkServer>();
    }

    Framework::Networking::NetworkPeerError Engine::Init(int32_t port, std::string &host, int32_t maxPlayers, std::string &password) {
        const auto result = _networkServer->Init(port, host, maxPlayers, password);
        if (result != Framework::Networking::NetworkPeerError::NETWORK_PEER_NONE) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to init the inner networking engine");
            return result;
        }

        _initialized = true;
        return Framework::Networking::NetworkPeerError::NETWORK_PEER_NONE;
    }

    void Engine::Shutdown() {
        if (_networkServer) {
            _networkServer->Shutdown();
        }
        Lifecycle::Shutdown();
    }

    void Engine::Update() {
        if (_networkServer) {
            _networkServer->Update();
        }
    }
} // namespace Framework::Integrations::Server::Networking
