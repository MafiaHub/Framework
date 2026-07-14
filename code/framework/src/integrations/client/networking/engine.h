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
#include <networking/network_client.h>

namespace Framework::Integrations::Client::Networking {
    class Engine final : public Shared::Networking::PeerEngine<Framework::Networking::NetworkClient> {
      public:
        [[nodiscard]] Utils::Result<void, Error> Init();

        [[nodiscard]] Utils::Result<void, Error> Connect(const std::string &host, int32_t port, const std::string &password = "") const;

        Framework::Networking::NetworkClient *GetNetworkClient() const {
            return _peer.get();
        }
    };
} // namespace Framework::Integrations::Client::Networking
