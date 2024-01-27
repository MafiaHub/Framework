#include "masterlist_client.h"

namespace Framework::Services {
    MasterlistClient::MasterlistClient(): MasterlistBase() {
        _client->set_default_headers({
            {"Content-Type", "application/json"}
        });
    }
    bool MasterlistClient::Fetch() {
        // Send the request
        // Make sure we can only handle valid responses
        const auto res = _client->Get("/rcon/servers");
        if (res->status != 200 && res->status != 201) {
            return false;
        }

        // TODO: return the result (how?) - body is a JSON payload in res->body
        return false;
    }
} // namespace Framework::Services
