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
    Utils::Result<void, Error> Engine::Init() {
        if (auto result = _peer->Init(); !result) {
            return result;
        }
        _initialized = true;
        return {};
    }

    Utils::Result<void, Error> Engine::Connect(const std::string &host, int32_t port, const std::string &password) const {
        if (!_peer) {
            return Error("Network client is not available");
        }

        return _peer->Connect(host, port, password);
    }
} // namespace Framework::Integrations::Client::Networking
