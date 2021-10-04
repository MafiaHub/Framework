#include "masterlist.h"

#include "logging/logger.h"
#include "utils/job_system.h"
#include "utils/time.h"

#include <firebase/functions.h>

namespace Framework::Services {
    void Masterlist::Update(External::Firebase::Wrapper *firebaseWrapper) {
        const auto now = Framework::Utils::Time::GetTime();

        if (now > _nextUpdate) {
            Framework::Utils::JobSystem::GetInstance()->EnqueueJob([this, firebaseWrapper]() {
                auto functions = firebase::functions::Functions::GetInstance(firebaseWrapper->GetApp());

                firebase::Variant req = firebase::Variant::EmptyMap();

                if (!GetHost().empty()) {
                    req.map()["host"] = GetHost();
                }

                req.map()["secret"]     = GetSecretKey();
                req.map()["port"]       = GetPort();
                req.map()["name"]       = GetServerName();
                req.map()["game"]       = GetGame();
                req.map()["version"]    = GetVersion();
                req.map()["mapname"]    = GetMap();
                req.map()["pass"]       = IsPassworded();
                req.map()["players"]    = GetPlayers();
                req.map()["maxPlayers"] = GetMaxPlayers();

                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVICES)->trace("[Masterlist] Pushing server info to backend");

                auto pushServer = functions->GetHttpsCallable("pushServer");
                auto result = pushServer.Call(req);
                while (result.status() != firebase::kFutureStatusComplete) {};
                if (result.error() != firebase::functions::kErrorNone) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVICES)->warn("[Masterlist] Push failed with error '{}' and message: '{}'", result.error(), result.error_message());
                    return false;
                }
                
                return true;
            });

            _nextUpdate = Framework::Utils::Time::GetTime() + _interval;
        }
    }
} // namespace Framework::Services
