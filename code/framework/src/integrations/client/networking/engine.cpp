/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

#include <logging/logger.h>

namespace Framework::Integrations::Client::Networking {
    Engine::Engine() {
        _networkClient = new Framework::Networking::NetworkClient;
    }

    bool Engine::Init() {
        if (!_networkClient->Init()) {
            return false;
        }

        return true;
    }

    bool Engine::Connect(const std::string& host, const int32_t port, const std::string password) {
        if (!_networkClient) {
            return false;
        }

        if (_networkClient->Connect(host, port, password) != Framework::Networking::ClientError::CLIENT_NONE) {
            return false;
        }

        return true;
    }

    bool Engine::Shutdown() {
        if (_networkClient) {
            _networkClient->Shutdown();
        }
        return true;
    }

    void Engine::Update() {
        if (_networkClient) {
            _networkClient->Update();
        }
    }
} // namespace Framework::Integrations::Client::Networking
