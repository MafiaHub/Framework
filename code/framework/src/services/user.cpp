/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "user.h"

#include "logging/logger.h"
#include "utils/job_system.h"

#include <firebase/functions.h>

namespace Framework::Services {
    UserServices::UserServices(External::Firebase::Wrapper *wrapper): _wrapper(wrapper) {}

    void UserServices::ValidateUserToken(const std::string &token, OnUserValidatedProc cb) {
        Framework::Utils::JobSystem::GetInstance()->EnqueueJob([this, &token, &cb]() {
            // TODO: Rework once we start using this functionality

            auto functions = firebase::functions::Functions::GetInstance(_wrapper->GetApp());

            firebase::Variant req = firebase::Variant::EmptyMap();

            req.map()["token"] = token;

            Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVICES)->trace("[UserServices] Validating user token: {}", token);

            auto pushServer = functions->GetHttpsCallable("api/user/verify");
            auto result     = pushServer.Call(req);
            while (result.status() != firebase::kFutureStatusComplete) {};
            if (result.error() != firebase::functions::kErrorNone) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVICES)
                    ->warn("[UserServices] Validation failed with error '{}' and message: '{}'", result.error(), result.error_message());
                cb(false, result.error_message());
                return false;
            }

            auto res                 = result.result()->data().map();
            const auto isValidated   = res["success"].AsBool().bool_value();
            const auto failureReason = res["error"].AsString().string_value();
            cb(isValidated, failureReason);
            return true;
        });
    }
} // namespace Framework::Services
