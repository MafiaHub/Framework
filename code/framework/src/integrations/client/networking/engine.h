/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/network_client.h>

namespace Framework::Integrations::Client::Networking {
    class Engine {
      private:
        Framework::Networking::NetworkClient *_networkClient = nullptr;

      public:
        Engine();

        bool Init();
        bool Shutdown();

        bool Connect(std::string &, int32_t, std::string password = "");

        void Update();

        Framework::Networking::NetworkClient *GetNetworkServer() const {
            return _networkClient;
        }
    };
} // namespace Framework::Integrations::Client::Networking
