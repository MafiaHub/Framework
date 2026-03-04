/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <unordered_map>

namespace Framework::Networking {
    enum class ConnectionError {
        CONNECTION_NONE,
        CONNECTION_PEER_FAILED,
        CONNECTION_PEER_NULL,
        CONNECTION_ALREADY_CONNECTED,
        CONNECTION_CONNECT_FAILED
    };

    enum class NetworkPeerError {
        NETWORK_PEER_NONE,
        NETWORK_PEER_INIT_FAILED
    };

    extern std::unordered_map<uint8_t, const char *> StartupResultString;
    extern std::unordered_map<uint8_t, const char *> ConnectionAttemptString;
}; // namespace Framework::Networking
