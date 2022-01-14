/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "logger.h"

#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Framework::Logging {
    Logger::Logger() {
        _sessionStart = std::chrono::system_clock::now();

        spdlog::flush_on(spdlog::level::err);
        spdlog::flush_every(std::chrono::seconds(2));

        // Support for async logging
        spdlog::init_thread_pool(10000, 4); // queue with 10K items and 4 backing threads.
    }

    std::shared_ptr<spdlog::logger> Logger::Get(const char *logName) {
        // Handle pause mode logs
        if (_loggingPaused) {
            constexpr auto suppressedLogger = "_suppressed_logger";
            if (auto logger = spdlog::get(suppressedLogger)) {
                return logger;
            }

            auto dummyLogger = spdlog::create<spdlog::sinks::null_sink_mt>(suppressedLogger);
            _loggers.emplace(suppressedLogger, dummyLogger);
            return dummyLogger;
        }

        if (auto logger = spdlog::get(logName)) {
            return logger;
        }

        auto consoleLogger = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleLogger->set_level(spdlog::level::debug);
        consoleLogger->set_pattern("[%H:%M:%S] [%n] [%^%l%$] %v");

        const auto fileLogName = _logFolder + "/" + _logName + ".log";
        auto fileLogger        = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fileLogName, _maxFileSize, _maxFileCount);
        fileLogger->set_level(spdlog::level::trace);

        std::vector<spdlog::sink_ptr> sinks {consoleLogger, fileLogger};
        auto spdLogger = std::make_shared<spdlog::async_logger>(logName, sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        spdLogger->set_level(spdlog::level::trace);
        spdlog::register_logger(spdLogger);

        try {
            _loggers.emplace(logName, spdLogger);
        }
        catch (std::exception &ex) {
        }

        return spdLogger;
    }

    Logger *GetInstance() {
        static Logger *_instance = nullptr;
        if (_instance == nullptr) {
            _instance = new Logger();
        }
        return _instance;
    }

    std::shared_ptr<spdlog::logger> GetLogger(const char *name) {
        return GetInstance()->Get(name);
    }
} // namespace Framework::Logging
