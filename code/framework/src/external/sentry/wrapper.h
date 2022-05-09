/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <sentry.h>
#include <string>

namespace Framework::External::Sentry {
    struct SystemInformation {
        std::string _cpuBrand;
        uint8_t _cpuProcessorsCount = 0;

        std::string _osVersion;
        int _osMajorVersion = -1;
        int _osMinorVersion = -1;
        int _osBuildNumber  = -1;
    };

    struct ScreenInformation {
        int _width  = 0;
        int _height = 0;
        bool _fullscreen = false;
    };

    struct UserInformation {
        std::string _fullName;
        std::string _name;
        std::string _userId;
    };

    struct GameInformation {
        std::string _title;
        std::string _version;
    };

    class Wrapper final {
      private:
        bool _valid = false;

      public:
        SentryError Init(const std::string &, const std::string &);
        SentryError Shutdown() const;

        SentryError CaptureEventMessage(int32_t level, const std::string &logger, const std::string &payload) const;
        SentryError CaptureEventException(const std::string &type, const std::string &message) const;

        SentryError SetSystemInformation(const SystemInformation &) const;
        SentryError SetScreenInformation(const ScreenInformation &) const;
        SentryError SetUserInformation(const UserInformation &) const;
        SentryError SetGameInformation(const GameInformation &) const;

        bool IsValid() const {
            return _valid;
        }
    };
} // namespace Framework::External::Sentry
