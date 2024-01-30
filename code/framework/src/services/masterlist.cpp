/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "masterlist.h"

#include <logging/logger.h>

namespace Framework::Services {
    MasterlistConnector::MasterlistConnector() {
        _client = std::make_shared<httplib::Client>("https://api.mafiahub.dev");
    }
    bool MasterlistConnector::Init(const std::string pushKey) {
        if (pushKey.empty()) {
            return false;
        }
        _client->set_default_headers({{"X-API-KEY", pushKey}, {"Content-Type", "application/json"}});

        _isInitialized = true;
        _pingThread    = std::thread(&MasterlistConnector::PingThread, this);
        _lastPingAt    = Utils::Time::GetTimePoint();
        return true;
    }
    bool MasterlistConnector::Shutdown() {
        {
            std::lock_guard lock(_mutex);
            _isInitialized = false;
        }
        _pingThread.join();
        return true;
    }
    void MasterlistConnector::PingThread() {
        while (_isInitialized) {
            std::lock_guard lock(_mutex);

            // Only ping every 5 seconds
            if (Utils::Time::GetDifference(Utils::Time::GetTimePoint(), _lastPingAt) < 5000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // Update the last ping time
            _lastPingAt = Utils::Time::GetTimePoint();

            // Build the payload
            httplib::Params params {
                {"gamemode", _storedInfo.gameMode},
                {"version", _storedInfo.version},
                {"max_players", std::to_string(_storedInfo.maxPlayers)},
                {"current_players", std::to_string(_storedInfo.currentPlayers)},
            };

            // Send the request
            // Make sure we can only handle valid responses
            const auto res = _client->Post("/rcon/ping", params);
            if (!res) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to ping masterlist server: {}", res.error());
            }
            if (res->status != 200 && res->status != 201) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to ping masterlist server: {} {}", res->status, res->body);
            }
        }
    }
    void MasterlistConnector::Ping(const ServerInfo &info) {
        std::lock_guard lock(_mutex);
        _storedInfo = info;
    }
} // namespace Framework::Services
