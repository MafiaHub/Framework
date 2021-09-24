#pragma once

#include "errors.h"

#include <chrono>
#include <http/webserver.h>
#include <logging/logger.h>
#include <scripting/engine.h>
#include <string>

namespace Framework::Integrations::Server {
    struct InstanceOptions {
        std::string modSlug;
        std::string modName;
        std::string modVersion;
        std::string bindName;
        std::string bindHost;
        int32_t bindPort;
        std::string bindPassword;
        int32_t maxPlayers;
    };

    class Instance {
      private:
        bool _alive;
        std::chrono::time_point<std::chrono::high_resolution_clock> _nextTick;

        Scripting::Engine *_scriptingEngine;
        HTTP::Webserver *_webServer;

      public:
        Instance();
        ~Instance();

        ServerError Init(InstanceOptions &);
        ServerError Shutdown();

        void InitEndpoints();

        void Update();

        void Run();

        bool IsAlive() const {
            return _alive;
        }

        Scripting::Engine *GetScriptingEngine() const {
            return _scriptingEngine;
        }

        HTTP::Webserver *GetWebserver() const {
            return _webServer;
        }
    };
} // namespace Framework::Integrations::Server
