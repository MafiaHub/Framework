#include "engine.h"

#include <logging/logger.h>

namespace Framework::Integrations::Server::Networking {
    Engine::Engine() {
        _peer = new Framework::Networking::NetworkServer;
    }

    bool Engine::Init(int32_t port, std::string &host, int32_t maxPlayers, std::string &password) {
        if (_peer->Init(port, host, maxPlayers, password) != Framework::Networking::SERVER_NONE) {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_SERVER)->critical("Failed to init the inner networking engine");
            return false;
        }

        return true;
    }

    bool Engine::Shutdown() {
        if (_peer) {
            _peer->Shutdown();
        }
        return true;
    }

    void Engine::Update() {
         if (_peer) {
            _peer->Update();
         }
    }
} // namespace Framework::Integrations::Server::Networking