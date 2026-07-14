/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "logger.h"

#include <spdlog/async.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Framework::Logging {
    namespace {
        class ForwardingSink final : public spdlog::sinks::base_sink<std::mutex> {
          public:
            explicit ForwardingSink(Logger *owner): _owner(owner) {}

          protected:
            void sink_it_(const spdlog::details::log_msg &msg) override {
                _owner->ForwardLog(static_cast<int>(msg.level), std::string(msg.logger_name.data(), msg.logger_name.size()), std::string(msg.payload.data(), msg.payload.size()));
            }

            void flush_() override {}

          private:
            Logger *_owner;
        };
    } // namespace

    Logger::Logger() {
        _sessionStart = std::chrono::system_clock::now();

        spdlog::flush_on(spdlog::level::err);
        spdlog::flush_every(std::chrono::seconds(2));

        // Support for async logging
        spdlog::init_thread_pool(10000, 4); // queue with 10K items and 4 backing threads.

        _forwardingSink = std::make_shared<ForwardingSink>(this);
        _forwardingSink->set_level(spdlog::level::trace);
    }

    std::shared_ptr<spdlog::logger> Logger::Get(const char *logName, bool async) {
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

        // If the logger already exists, return it
        if (auto logger = spdlog::get(logName)) {
            return logger;
        }

        // Build the different sinks
        const auto consoleLogger = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleLogger->set_level(spdlog::level::debug);
        consoleLogger->set_pattern("[%H:%M:%S] [%^%l%$] [%n] %v");

        const auto fileLogName = _logFolder + "/" + _logName + ".log";
        const auto fileLogger  = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fileLogName, _maxFileSize, _maxFileCount);
        fileLogger->set_level(spdlog::level::trace);

        if (!_ringbufferSink) {
            _ringbufferSink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(_maxRingBufferSize);
            _ringbufferSink->set_level(spdlog::level::debug);
        }
        std::vector<spdlog::sink_ptr> sinks {consoleLogger, fileLogger, _ringbufferSink, _forwardingSink};

        // Create our logging instance
        std::shared_ptr<spdlog::logger> spdLogger;

        // Create the logger depending on the type we want
        if (async) {
            spdLogger = std::make_shared<spdlog::async_logger>(logName, sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        }
        else {
            spdLogger = std::make_shared<spdlog::logger>(logName, sinks.begin(), sinks.end());
        }

        spdLogger->set_level(spdlog::level::trace);

        try {
            spdlog::register_logger(spdLogger);
        }
        catch (std::exception &ex) {
            return nullptr;
        }

        _loggers.emplace(logName, spdLogger);

        return spdLogger;
    }

    void Logger::SetLogForwarder(LogForwarder forwarder, int threshold) {
        std::lock_guard lock(_forwarderMutex);
        _forwarder          = std::move(forwarder);
        _forwarderThreshold = threshold;
    }

    void Logger::ForwardLog(int level, const std::string &name, const std::string &message) {
        LogForwarder forwarder;
        {
            std::lock_guard lock(_forwarderMutex);
            if (!_forwarder || level < _forwarderThreshold) {
                return;
            }
            forwarder = _forwarder;
        }
        forwarder(level, name, message);
    }

    Logger *GetInstance() {
        static Logger instance;
        return &instance;
    }

    std::shared_ptr<spdlog::logger> GetLogger(const char *name, bool async) {
        return GetInstance()->Get(name, async);
    }
} // namespace Framework::Logging
