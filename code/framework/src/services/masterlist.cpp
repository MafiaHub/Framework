#include "masterlist.h"

namespace Framework::Services {
    Masterlist::Masterlist(MasterlistType type): _type(type), _client(nullptr) {
        return;
    }

    bool Masterlist::Init(std::string &pushKey){
        _client = new httplib::Client("https://api.mafiahub.dev", 443);

        if(_type == MasterlistType::Server){
            _defaultHeaders = httplib::Headers{
                {"X-API-KEY", pushKey},
                {"Content-Type", "application/json"}
            };
        }

        _isInitialized = true;

        return true;
    }

    bool Masterlist::Shutdown(){
        _isInitialized = false;
        // TODO: Shutdown masterlist
        return true;
    }

    void Masterlist::Fetch(){
        // Only fetch if we are initialized
        if(!_isInitialized){
            return;
        }

        // Only the client can fetch from masterlist
        if(_type != MasterlistType::Client){
            return;
        }

        // Send the request
        // Make sure we can only handle valid responses
        const auto res = _client->Get("/rcon/servers");
        if(res->status != 200 && res->status != 201){
            return;
        }

        // TODO: return the result (how?) - body is a JSON payload in res->body
        return;
    }

    bool Masterlist::Ping(){
        // Only ping if we are initialized
        if(!_isInitialized){
            return false;
        }

        // Only the server can push to masterlist
        if(_type != MasterlistType::Server){
            return false;
        }

        // Only ping every 10 seconds (for now)
        if(Utils::Time::GetDifference(Utils::Time::GetTimePoint(), _lastPingAt) < 10000){
            return false;
        }

        // Build the payload
        // TODO: grab from the networking service, and pass as parameter to the ping method, then add it
        httplib::Params params{
            {"gamemode", "my_super_gm"},
            {"version", "1.0.0"},
            {"max_players", "10"},
            {"current_players", "2"},
        };

        // Send the request
        // Make sure we can only handle valid responses
        const auto res = _client->Post("/rcon/ping", _defaultHeaders, params);
        if(res->status != 200 && res->status != 201){
            return false;
        }

        // Update the last ping time
        _lastPingAt = Utils::Time::GetTimePoint();
        return true;
    }
}