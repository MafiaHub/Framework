/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "external/firebase/wrapper.h"

#include <functional>
#include <string>

namespace Framework::Services {
    class UserServices {
      private:
        int32_t _interval   = 60000;
        int64_t _nextUpdate = 0;
        External::Firebase::Wrapper *_wrapper;

      public:
        using OnUserValidatedProc = std::function<void(bool isValidated, const std::string &failureReason)>;
        UserServices(External::Firebase::Wrapper *);

        void ValidateUserToken(const std::string &token, OnUserValidatedProc cb);
    };
} // namespace Framework::Services
