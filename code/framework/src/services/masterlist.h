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
#include <nlohmann/json.hpp>

#include <utils/time.h>

namespace Framework::Services {
    // Default URL constants
    static const std::string DEFAULT_API_URL = "https://api.mafiahub.dev";
    static const std::string DEFAULT_MASTERLIST_URL = "https://masterlist.mafia.mp";
    // Time constants (in milliseconds)
    static const int64_t REAUTH_THRESHOLD_MS = 24 * 60 * 60 * 1000; // 1 day in milliseconds

    struct ServerInfo {
        std::string gameMode;
        std::string version;
        int32_t maxPlayers;
        int32_t currentPlayers;
    };
    class MasterlistConnector {
        std::mutex _mutex;
        std::thread _pingThread;
        std::shared_ptr<httplib::Client> _client;
        std::string _pushKey {};
        ServerInfo _storedInfo {};
        Utils::Time::TimePoint _lastPingAt {};
        bool _isInitialized {false};

        // URLs for API and masterlist
        std::string _apiUrl {DEFAULT_API_URL};
        std::string _masterlistUrl {DEFAULT_MASTERLIST_URL};

        // JWT token and expiration
        std::string _jwtToken {};
        Utils::Time::TimePoint _tokenExpirationTime {};

        void PingThread();
        bool Authenticate();

      public:
        MasterlistConnector(const std::string &apiUrl = DEFAULT_API_URL, 
                           const std::string &masterlistUrl = DEFAULT_MASTERLIST_URL);

        bool Init(const std::string &);
        bool Shutdown();

        void Ping(const ServerInfo &);
        bool IsInitialized() const {
            return _isInitialized;
        }
    };
}; // namespace Framework::Services
