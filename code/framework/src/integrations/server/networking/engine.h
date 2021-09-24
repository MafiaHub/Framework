#pragma once

#include <networking/network_server.h>

namespace Framework::Integrations::Server::Networking {
    class Engine {
      private:
        Framework::Networking::NetworkServer *_networkServer = nullptr;

      public:
        Engine();

        bool Init(int32_t, std::string &, int32_t, std::string &);
        bool Shutdown();

        void Update();

        Framework::Networking::NetworkServer *GetNetworkServer() const {
            return _networkServer;
        }
    };
} // namespace Framework::Integrations::Server::Networking