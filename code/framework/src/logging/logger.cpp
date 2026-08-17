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
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <vector>

namespace Framework::Logging {
    namespace {
        class ForwardingSink final: public spdlog::sinks::base_sink<std::mutex> {
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

        // Only touches an atomic, so no locking is needed.
        class CountingSink final: public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
          public:
            explicit CountingSink(Logger *owner): _owner(owner) {}

          protected:
            void sink_it_(const spdlog::details::log_msg &) override {
                _owner->CountLogEvent();
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

        // Support for async logging. A single worker keeps messages in order; more
        // workers may dequeue and write them out of order.
        spdlog::init_thread_pool(10000, 1); // queue with 10K items and 1 backing thread.

        _forwardingSink = std::make_shared<ForwardingSink>(this);
        _forwardingSink->set_level(spdlog::level::off); // opened by SetLogForwarder
    }

    void Logger::EnsureSharedSinks() {
        if (_consoleSink) {
            return;
        }

        const auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_level(spdlog::level::debug);
        consoleSink->set_pattern("[%H:%M:%S] [%^%l%$] [%n] %v");
        _consoleSink = consoleSink;

        const auto fileLogName = _logFolder + "/" + _logName + ".log";
        _fileSink              = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fileLogName, _maxFileSize, _maxFileCount);
        _fileSink->set_level(spdlog::level::trace);

        if (!_ringbufferSink) {
            _ringbufferSink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(_maxRingBufferSize);
            _ringbufferSink->set_level(spdlog::level::debug);
        }

        _countingSink = std::make_shared<CountingSink>(this);
        _countingSink->set_level(spdlog::level::trace);
    }

    std::shared_ptr<spdlog::logger> Logger::Get(const char *logName, bool async) {
        // The per-thread cache in GetLogger absorbs the hot path, so this can hold the
        // mutex throughout; that also keeps creation ordered against Shutdown().
        std::lock_guard lock(_creationMutex);

        // Handle pause mode logs
        if (_loggingPaused.load(std::memory_order_relaxed)) {
            constexpr auto suppressedLogger = "_suppressed_logger";
            if (auto logger = spdlog::get(suppressedLogger)) {
                return logger;
            }
            return spdlog::create<spdlog::sinks::null_sink_mt>(suppressedLogger);
        }

        // If the logger already exists, return it
        if (auto logger = spdlog::get(logName)) {
            return logger;
        }

        EnsureSharedSinks();
        std::vector<spdlog::sink_ptr> sinks {_consoleSink, _fileSink, _ringbufferSink, _countingSink, _forwardingSink};

        // Create our logging instance
        std::shared_ptr<spdlog::logger> spdLogger;

        // Create the logger depending on the type we want. After Shutdown() the thread
        // pool is gone, so late loggers silently fall back to synchronous.
        const auto threadPool = spdlog::thread_pool();
        if (async && threadPool) {
            spdLogger = std::make_shared<spdlog::async_logger>(logName, sinks.begin(), sinks.end(), threadPool, spdlog::async_overflow_policy::block);
        }
        else {
            spdLogger = std::make_shared<spdlog::logger>(logName, sinks.begin(), sinks.end());
        }

        spdLogger->set_level(spdlog::level::trace);

        try {
            spdlog::register_logger(spdLogger);
        }
        catch (std::exception &) {
            if (auto existing = spdlog::get(logName)) {
                return existing;
            }
            // Unregistered but functional; callers must never receive nullptr.
        }

        return spdLogger;
    }

    void Logger::Shutdown() {
        // Ordered against Get() so no logger can bind the thread pool mid-teardown.
        std::lock_guard lock(_creationMutex);
        _cacheGeneration.fetch_add(1, std::memory_order_release);
        spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) {
            logger->flush();
        });
        spdlog::shutdown();
    }

    void Logger::SetLogForwarder(LogForwarder forwarder, int threshold) {
        std::lock_guard lock(_forwarderMutex);
        _forwarder          = std::move(forwarder);
        _forwarderThreshold = threshold;
        // Filter at the sink so sub-threshold messages skip the mutex and string copies.
        _forwardingSink->set_level(_forwarder ? static_cast<spdlog::level::level_enum>(threshold) : spdlog::level::off);
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
        // spdlog::get locks the global registry per call, so cache handles per thread.
        // Keyed by name value, not pointer: callers may pass transient buffers.
        struct CacheEntry {
            std::string name;
            bool async;
            uint32_t generation;
            std::shared_ptr<spdlog::logger> logger;
        };
        thread_local std::vector<CacheEntry> cache;

        const auto generation = Logger::GetCacheGeneration();
        for (const auto &entry : cache) {
            if (entry.name == name && entry.async == async && entry.generation == generation) {
                return entry.logger;
            }
        }

        auto logger = GetInstance()->Get(name, async);
        if (logger) {
            std::erase_if(cache, [generation](const CacheEntry &entry) {
                return entry.generation != generation;
            });
            cache.push_back({name, async, generation, logger});
        }
        return logger;
    }
} // namespace Framework::Logging
