/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <httplib.h>

#include <chrono>
#include <cstdint>
#include <random>

// A port for a whole test server, which binds UDP for the game and TCP for HTTP on the same number.
namespace TestNetwork {
    inline bool UdpPortFree(int32_t port) {
        const auto probe = ::socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
        if (probe == INVALID_SOCKET) {
            return false;
        }
#else
        if (probe < 0) {
            return false;
        }
#endif
        sockaddr_in address {};
        address.sin_family      = AF_INET;
        address.sin_port        = htons(static_cast<uint16_t>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const bool isFree       = ::bind(probe, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0;
#ifdef _WIN32
        ::closesocket(probe);
#else
        ::close(probe);
#endif
        return isFree;
    }

    // A port nothing listens on, as far as binding it on loopback can tell; 0 when eight tries found none. A server
    // cannot be initialised twice, so the port is settled before Init rather than by retrying it. Both protocols are
    // probed: a TCP bind says nothing of UDP, and Windows keeps ranges of UDP back.
    inline int32_t FreePort() {
        std::mt19937 random(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        for (int attempt = 0; attempt < 8; ++attempt) {
            const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
            httplib::Server probe;
            if (probe.bind_to_port("127.0.0.1", candidate)) {
                probe.stop();
                if (UdpPortFree(candidate)) {
                    return candidate;
                }
            }
        }
        return 0;
    }
} // namespace TestNetwork
