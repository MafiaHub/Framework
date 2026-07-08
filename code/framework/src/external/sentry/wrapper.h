/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <utils/lifecycle.h>

#include <utils/error.h>
#include <utils/result.h>

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
        int _width       = 0;
        int _height      = 0;
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

    class Wrapper final : public Framework::Lifecycle {
      public:
        [[nodiscard]] Utils::Result<void, Framework::Error> Init(const std::string &, const std::string &);
        void Shutdown() override;

        Utils::Result<void, Framework::Error> CaptureEventMessage(int32_t level, const std::string &logger, const std::string &payload) const;
        Utils::Result<void, Framework::Error> CaptureEventException(const std::string &type, const std::string &message) const;

        Utils::Result<void, Framework::Error> AddAttachment(const std::string &path) const;

        Utils::Result<void, Framework::Error> SetSystemInformation(const SystemInformation &) const;
        Utils::Result<void, Framework::Error> SetScreenInformation(const ScreenInformation &) const;
        Utils::Result<void, Framework::Error> SetUserInformation(const UserInformation &) const;
        Utils::Result<void, Framework::Error> SetGameInformation(const GameInformation &) const;

        bool IsValid() const {
            return IsInitialized();
        }
    };
} // namespace Framework::External::Sentry
