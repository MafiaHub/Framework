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

    Utils::Result<void, Error> Engine::Init() {
        if (auto result = _networkClient->Init(); !result) {
            return result;
        }
        _initialized = true;
        return {};
    }

    Utils::Result<void, Error> Engine::Connect(const std::string &host, const int32_t port, const std::string password) const {
        if (!_networkClient) {
            return Error("Network client is not available");
        }

        return _networkClient->Connect(host, port, password);
    }

    void Engine::Shutdown() {
        if (_networkClient) {
            _networkClient->Shutdown();
        }
        Lifecycle::Shutdown();
    }

    void Engine::Update() {
        if (_networkClient) {
            _networkClient->Update();
        }
    }
} // namespace Framework::Integrations::Client::Networking
