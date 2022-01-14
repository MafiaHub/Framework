/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <chrono>
#include <map>
#include <spdlog/async.h>
#include <spdlog/spdlog.h>
#include <unordered_map>

#define FRAMEWORK_INNER_NETWORKING   "Networking"
#define FRAMEWORK_INNER_SCRIPTING    "Scripting"
#define FRAMEWORK_INNER_HTTP         "HTTP"
#define FRAMEWORK_INNER_SERVICES     "Services"
#define FRAMEWORK_INNER_INTEGRATIONS "Integrations"
#define FRAMEWORK_INNER_JOBS         "Jobs"
#define FRAMEWORK_INNER_LAUNCHER     "Launcher"
#define FRAMEWORK_INNER_UTILS        "Utils"
#define FRAMEWORK_INNER_GRAPHICS     "Graphics"

#define FRAMEWORK_INNER_SERVER "Server"
#define FRAMEWORK_INNER_CLIENT "Client"

namespace Framework::Logging {
    class Logger final {
      private:
        std::chrono::time_point<std::chrono::system_clock> _sessionStart;
        std::unordered_map<const char *, std::shared_ptr<spdlog::logger>> _loggers;
        std::string _logName   = "framework";
        std::string _logFolder = "logs";
        size_t _maxFileSize    = 1024 * 1024 * 10;
        size_t _maxFileCount   = 10;
        bool _loggingPaused    = false;

      public:
        Logger();
        ~Logger() = default;

        std::shared_ptr<spdlog::logger> Get(const char *moduleName);

        void SetLogName(const std::string &name) {
            _logName = name;
        }

        const std::string &GetLogName() const {
            return _logName;
        }

        void SetLogFolder(const std::string &folder) {
            _logFolder = folder;
        }

        const std::string &GetLogFolder() const {
            return _logFolder;
        }

        void SetMaxFileSize(size_t size) {
            _maxFileSize = size;
        }

        size_t GetMaxFileSize() const {
            return _maxFileSize;
        }

        bool IsLoggingPaused() const {
            return _loggingPaused;
        }

        void PauseLogging(bool state) {
            _loggingPaused = state;
        }

        void SetMaxFileCount(size_t count) {
            _maxFileCount = count;
        }

        size_t GetMaxFileCount() const {
            return _maxFileCount;
        }
    };

    extern Logger *GetInstance();

    extern std::shared_ptr<spdlog::logger> GetLogger(const char *name);
} // namespace Framework::Logging
