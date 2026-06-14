/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/error.h>
#include <utils/lifecycle.h>
#include <utils/result.h>

#include <networking/network_client.h>

namespace Framework::Integrations::Client::Networking {
    class Engine final : public Framework::Lifecycle {
      private:
        std::unique_ptr<Framework::Networking::NetworkClient> _networkClient {};

      public:
        Engine();

        [[nodiscard]] Utils::Result<void, Error> Init();
        void Shutdown() override;

        [[nodiscard]] Utils::Result<void, Error> Connect(const std::string &, const int32_t, const std::string password = "") const;

        void Update() override;

        Framework::Networking::NetworkClient *GetNetworkClient() const {
            return _networkClient.get();
        }
    };
} // namespace Framework::Integrations::Client::Networking
