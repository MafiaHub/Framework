#pragma once

#include <string>
#include <memory>

#include <httplib.h>

namespace Framework::Services {
    struct ServerInfo {
        std::string gameMode;
        std::string version;
        int32_t maxPlayers;
        int32_t currentPlayers;
    };
    class MasterlistBase {
        protected:
            std::shared_ptr<httplib::Client> _client;
        public:
            MasterlistBase();
    };
};
