/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

#include <logging/logger.h>

namespace Framework::Integrations::Client::Networking {
    Engine::Engine() {
        _networkClient = std::make_unique<Framework::Networking::NetworkClient>();
    }

    bool Engine::Init() const {
        return _networkClient->Init() == Framework::Networking::CLIENT_NONE;
    }

    bool Engine::Connect(const std::string &host, const int32_t port, const std::string password) const {
        if (!_networkClient) {
            return false;
        }

        if (_networkClient->Connect(host, port, password) != Framework::Networking::ClientError::CLIENT_NONE) {
            return false;
        }

        return true;
    }

    bool Engine::Shutdown() const {
        if (_networkClient) {
            _networkClient->Shutdown();
        }
        return true;
    }

    void Engine::Update() const {
        if (_networkClient) {
            _networkClient->Update();
        }
    }
} // namespace Framework::Integrations::Client::Networking
