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

#include <cstdint>
#include <map>
#include <string>

namespace Framework::External::Sentry {
    // Mirrors sentry_level_e.
    enum class Level : int32_t {
        Trace   = -2,
        Debug   = -1,
        Info    = 0,
        Warning = 1,
        Error   = 2,
        Fatal   = 3,
    };

    struct ContextValue {
        enum class Type { String, Int, Double, Bool };

        Type type;
        std::string string;
        int32_t integer = 0;
        double number   = 0.0;
        bool boolean    = false;

        ContextValue(const char *value): type(Type::String), string(value) {}
        ContextValue(std::string value): type(Type::String), string(std::move(value)) {}
        ContextValue(int32_t value): type(Type::Int), integer(value) {}
        ContextValue(double value): type(Type::Double), number(value) {}
        ContextValue(bool value): type(Type::Bool), boolean(value) {}
    };

    using ContextFields = std::map<std::string, ContextValue>;

    struct InitOptions {
        std::string dsn;
        std::string handlerPath;
        std::string release;
        std::string environment;
        int maxBreadcrumbs = 100;
    };

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
        [[nodiscard]] Utils::Result<void, Framework::Error> Init(const InitOptions &);
        void Shutdown() override;

        Utils::Result<void, Framework::Error> CaptureEventMessage(int32_t level, const std::string &logger, const std::string &payload) const;
        Utils::Result<void, Framework::Error> CaptureEventException(const std::string &type, const std::string &message) const;

        Utils::Result<void, Framework::Error> AddAttachment(const std::string &path) const;

        void SetTag(const std::string &key, const std::string &value) const;
        void RemoveTag(const std::string &key) const;
        void SetContext(const std::string &name, const ContextFields &fields) const;
        void AddBreadcrumb(const std::string &category, const std::string &message, Level level = Level::Info, const ContextFields &data = {}) const;

        Utils::Result<void, Framework::Error> SetSystemInformation(const SystemInformation &) const;
        Utils::Result<void, Framework::Error> SetScreenInformation(const ScreenInformation &) const;
        Utils::Result<void, Framework::Error> SetUserInformation(const UserInformation &) const;
        Utils::Result<void, Framework::Error> SetGameInformation(const GameInformation &) const;

        bool IsValid() const {
            return IsInitialized();
        }
    };
} // namespace Framework::External::Sentry
