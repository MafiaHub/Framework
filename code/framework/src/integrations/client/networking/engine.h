/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/lifecycle.h>

#include <networking/network_client.h>

namespace Framework::Integrations::Client::Networking {
    class Engine : public Framework::Lifecycle {
      private:
        std::unique_ptr<Framework::Networking::NetworkClient> _networkClient {};

      public:
        Engine();

        [[nodiscard]] Framework::Networking::NetworkPeerError Init();
        void Shutdown() override;

        bool Connect(const std::string &, const int32_t, const std::string password = "") const;

        void Update() override;

        Framework::Networking::NetworkClient *GetNetworkClient() const {
            return _networkClient.get();
        }
    };
} // namespace Framework::Integrations::Client::Networking
