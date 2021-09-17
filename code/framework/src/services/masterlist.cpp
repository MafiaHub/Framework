#include "masterlist.h"

#include "logging/logger.h"
#include "utils/job_system.h"
#include "utils/time.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#define SERVICES_MASTERLIST_URI "https://us-central1-mafiahub-3ebed.cloudfunctions.net"

namespace Framework::Services {
    void Masterlist::Update() {
        const auto now = Framework::Utils::Time::GetTime();

        if (now > _nextUpdate) {
            Framework::Utils::JobSystem::GetInstance()->EnqueueJob([this]() {
                httplib::Client cli(SERVICES_MASTERLIST_URI);

                nlohmann::json req;

                if (!GetHost().empty()) {
                    req["host"] = GetHost();
                }

                req["secret"]     = GetSecretKey();
                req["port"]       = GetPort();
                req["name"]       = GetServerName();
                req["game"]       = GetGame();
                req["version"]    = GetVersion();
                req["mapname"]    = GetMap();
                req["pass"]       = IsPassworded();
                req["players"]    = GetPlayers();
                req["maxPlayers"] = GetMaxPlayers();
                const auto json   = req.dump(0);

                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVICES)
                    ->trace("[Masterlist] Pushing server info to backend");
                const auto res = cli.Post("/pushServer", json.c_str(), "application/json");

                if (res && res->status != 200) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVICES)
                        ->warn("[Masterlist] Push failed with error {} and message {}", res->status, res->body);

                    return false;
                } else if (!res) {
                    return false;
                }

                return true;
            });

            _nextUpdate = Framework::Utils::Time::GetTime() + _interval;
        }
    }
} // namespace Framework::Services
