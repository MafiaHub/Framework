#pragma once

#include <sentry.h>
#include <string>


namespace Framework::Integrations {
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

    class CrashReporter final {
      private:
        bool _valid = false;

      public:
        bool Init(const std::string &, const std::string &);
        bool Shutdown();

        void CaptureEventMessage(int32_t level, const std::string &logger, const std::string &payload);
        void CaptureEventException(const std::string &type, const std::string &message);

        void SetSystemInformation(const SystemInformation &);
        void SetScreenInformation(const ScreenInformation &);
        void SetUserInformation(const UserInformation &);
        void SetGameInformation(const GameInformation &);

        bool IsValid() const {
            return _valid;
        }
    };
} // namespace Framework::Integrations
