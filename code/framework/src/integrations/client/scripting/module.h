#pragma once

#include <scripting/client_engine.h>
#include <world/client.h>

namespace Framework::Integrations::Client::Scripting {
    class ClientScriptingModule {
      private:
        std::shared_ptr<Framework::Scripting::ClientEngine> _clientEngine;
        std::shared_ptr<World::ClientEngine> _world;

      public:
        ClientScriptingModule(std::shared_ptr<World::ClientEngine>);

        ~ClientScriptingModule() = default;

        bool Init(Framework::Scripting::SDKRegisterCallback);

        bool Shutdown();

        std::shared_ptr<Framework::Scripting::ClientEngine> GetEngine() const {
            return _clientEngine;
        }

        std::shared_ptr<World::ClientEngine> GetWorldEngine() const {
            return _world;
        }
    };
}
