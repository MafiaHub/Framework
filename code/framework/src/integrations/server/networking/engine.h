/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/error.h>
#include <utils/result.h>

#include <integrations/shared/networking/peer_engine.h>
#include <networking/network_server.h>

namespace Framework::Integrations::Server::Networking {
    class Engine final : public Shared::Networking::PeerEngine<Framework::Networking::NetworkServer> {
      public:
        [[nodiscard]] Utils::Result<void, Error> Init(const std::string &host, int32_t port, int32_t maxPlayers, const std::string &password);

        Framework::Networking::NetworkServer *GetNetworkServer() const {
            return _peer.get();
        }
    };
} // namespace Framework::Integrations::Server::Networking
