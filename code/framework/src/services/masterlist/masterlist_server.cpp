#include "masterlist_server.h"
#include "logging/logger.h"

namespace Framework::Services {
    MasterlistServer::MasterlistServer(): MasterlistBase() {}
    bool MasterlistServer::SetupServer(const std::string pushKey) {
        if (pushKey.empty()) {
            return false;
        }
        _client->set_default_headers({
            {"X-API-KEY", pushKey}, 
            {"Content-Type", "application/json"}});
        
        _isInitialized = true;
        return true;
    }
    bool MasterlistServer::Shutdown() {
        _isInitialized = false;
        return true;
    }
    bool MasterlistServer::Ping(const ServerInfo &info) {
        // Only ping if we are initialized
        if (!_isInitialized) {
            return false;
        }

        // Only ping every 10 seconds (for now)
        if (Utils::Time::GetDifference(Utils::Time::GetTimePoint(), _lastPingAt) < 10000) {
            return false;
        }

        // Update the last ping time
        _lastPingAt = Utils::Time::GetTimePoint();

        // Build the payload
        httplib::Params params {
            {"gamemode", info.gameMode},
            {"version", info.version},
            {"max_players", std::to_string(info.maxPlayers)},
            {"current_players", std::to_string(info.currentPlayers)},
        };

        // Send the request
        // Make sure we can only handle valid responses
        // TODO:  Failed to ping masterlist server: 400 {"message":["max_players must be a number conforming to the specified constraints","current_players must be a number conforming to the specified constraints"],"error":"Bad Request","statusCode":400}
        const auto res = _client->Post("/rcon/ping", params);
        if (!res) {
            Logging::GetLogger("masterlist")->error("Failed to ping masterlist server: {}", res.error());
            return false;
        }
        if (res->status != 200 && res->status != 201) {
            Logging::GetLogger("masterlist")->error("Failed to ping masterlist server: {} {}", res->status, res->body);
            return false;
        }
        return true;
    }
} // namespace Framework::Services
