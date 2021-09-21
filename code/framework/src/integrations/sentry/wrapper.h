#pragma once

#include "errors.h"

#include <sentry.h>
#include <string>

namespace Framework::Integrations::Sentry {
    struct SystemInformation {
        std::string _cpuBrand;
        uint8_t _cpuProcessorsCount = 0;

        std::string _osVersion;
        uint32_t _osMajorVersion = -1;
        uint32_t _osMinorVersion = -1;
        uint32_t _osBuildNumber  = -1;
    };

    struct ScreenInformation {
        uint32_t _width  = 0;
        uint32_t _height = 0;
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
        SentryError Shutdown();

        SentryError CaptureEventMessage(int32_t level, const std::string &logger, const std::string &payload);
        SentryError CaptureEventException(const std::string &type, const std::string &message);

        SentryError SetSystemInformation(const SystemInformation &);
        SentryError SetScreenInformation(const ScreenInformation &);
        SentryError SetUserInformation(const UserInformation &);
        SentryError SetGameInformation(const GameInformation &);

        bool IsValid() const {
            return _valid;
        }
    };
} // namespace Framework::Integrations
