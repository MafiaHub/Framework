#pragma once

#include "masterlist.h"

#include <utils/time.h>

namespace Framework::Services {
    class MasterlistServer final : public MasterlistBase {
      private:
        std::string _pushKey {};
        Utils::Time::TimePoint _lastPingAt{};
        bool _isInitialized {false};
      public:
        MasterlistServer();

        bool SetupServer(const std::string);
        bool Shutdown();

        bool Ping(const ServerInfo&);
        bool IsInitialized() const {
            return _isInitialized;
        }
    };
}; // namespace Framework::Services
