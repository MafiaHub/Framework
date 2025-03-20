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
    MasterlistConnector::MasterlistConnector(const std::string &apiUrl, const std::string &masterlistUrl) {
        _apiUrl = apiUrl.empty() ? DEFAULT_API_URL : apiUrl;
        _masterlistUrl = masterlistUrl.empty() ? DEFAULT_MASTERLIST_URL : masterlistUrl;
        _client = std::make_shared<httplib::Client>(_apiUrl);
    }

    bool MasterlistConnector::Authenticate() {
        _client->set_default_headers({{"X-API-KEY", _pushKey}, {"Content-Type", "application/json"}});

        const auto res = _client->Get("/rcon/auth");
        if (!res) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to authenticate with masterlist: {}", res.error());
            return false;
        }

        if (res->status != 200) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to authenticate with masterlist: {} {}", res->status, res->body);
            return false;
        }

        try {
            auto jsonResponse = nlohmann::json::parse(res->body);
            if (!jsonResponse.contains("result") || !jsonResponse["result"].is_string()) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Invalid authentication response format");
                return false;
            }

            _jwtToken = jsonResponse["result"];
            // Token valid for 7 days
            _tokenExpirationTime = Utils::Time::GetTimePoint() + std::chrono::hours(24 * 7);
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Successfully authenticated with masterlist, token valid for 7 days");
            return true;
        }
        catch (const std::exception &e) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to parse authentication response: {}", e.what());
            return false;
        }
    }

    bool MasterlistConnector::Init(const std::string &pushKey) {
        if (pushKey.empty()) {
            return false;
        }
        
        _pushKey = pushKey;
        
        if (!Authenticate()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to initialize masterlist connector: Authentication failed");
            return false;
        }

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
            // Only ping every 5 seconds
            if (Utils::Time::GetDifference(Utils::Time::GetTimePoint(), _lastPingAt) < 5000) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            {
                std::lock_guard lock(_mutex);

                // Check if token is about to expire (re-auth if less than 1 day left)
                if (Utils::Time::GetDifference(_tokenExpirationTime, Utils::Time::GetTimePoint()) < 24 * 60 * 60 * 1000) {
                    if (!Authenticate()) {
                        // If authentication fails, try again later
                        _lastPingAt = Utils::Time::GetTimePoint();
                        continue;
                    }
                }
                
                // Update the last ping time
                _lastPingAt = Utils::Time::GetTimePoint();

                // Create a new client for the masterlist URL
                auto masterlistClient = std::make_shared<httplib::Client>(_masterlistUrl);
                
                // Set the JWT token in the Authorization header
                masterlistClient->set_default_headers({
                    {"Authorization", "Bearer " + _jwtToken}, 
                    {"Content-Type", "application/json"}
                });

                // Build the payload
                httplib::Params params {
                    {"gamemode", _storedInfo.gameMode},
                    {"version", _storedInfo.version},
                    {"max_players", std::to_string(_storedInfo.maxPlayers)},
                    {"current_players", std::to_string(_storedInfo.currentPlayers)},
                };

                // Send the request to the masterlist
                const auto res = masterlistClient->Post("/ping", params);
                if (!res) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to ping masterlist server: {}", res.error());
                }
                else if (res->status != 200 && res->status != 201) {
                    Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to ping masterlist server: {} {}", res->status, res->body);
                }
            }
        }
    }

    void MasterlistConnector::Ping(const ServerInfo &info) {
        std::lock_guard lock(_mutex);
        _storedInfo = info;
    }
} // namespace Framework::Services
