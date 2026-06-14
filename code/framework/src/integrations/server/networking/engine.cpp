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

    Utils::Result<void, Error> Engine::Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password) {
        if (auto result = _networkServer->Init(host, port, maxPlayers, password); !result) {
            return result;
        }

        _initialized = true;
        return {};
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
