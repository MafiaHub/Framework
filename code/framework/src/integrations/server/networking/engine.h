/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <networking/network_server.h>

namespace Framework::Integrations::Server::Networking {
    class Engine {
      private:
        std::unique_ptr<Framework::Networking::NetworkServer> _networkServer{};

      public:
        Engine();

        bool Init(int32_t, std::string &, int32_t, std::string &);
        bool Shutdown();

        void Update();

        Framework::Networking::NetworkServer *GetNetworkServer() const {
            return _networkServer.get();
        }
    };
} // namespace Framework::Integrations::Server::Networking
