/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <httplib.h>

#include <utils/time.h>

namespace Framework::Services {
    struct ServerInfo {
        std::string gameMode;
        std::string version;
        int32_t maxPlayers;
        int32_t currentPlayers;
    };
    class MasterlistConnector {
      private:
        std::mutex _mutex;
        std::thread _pingThread;
        std::shared_ptr<httplib::Client> _client;
        std::string _pushKey {};
        ServerInfo _storedInfo {};
        Utils::Time::TimePoint _lastPingAt {};
        bool _isInitialized {false};

        void PingThread();

      public:
        MasterlistConnector();

        bool Init(const std::string);
        bool Shutdown();

        void Ping(const ServerInfo&);
        bool IsInitialized() const {
            return _isInitialized;
        }
    };
}; // namespace Framework::Services
