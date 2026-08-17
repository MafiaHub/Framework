/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>
#include <string>

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
    // level is the raw spdlog::level value.
    using LogForwarder = std::function<void(int level, const std::string &name, const std::string &message)>;

    class Logger final {
      private:
        [[maybe_unused]] std::chrono::time_point<std::chrono::system_clock> _sessionStart;
        // Serializes logger/sink creation; the fast path (already-registered logger) never takes it.
        std::mutex _creationMutex;
        std::string _logName   = "framework";
        std::string _logFolder = "logs";
        size_t _maxFileSize    = 1024 * 1024 * 10;
        size_t _maxFileCount   = 10;
        std::atomic<bool> _loggingPaused {false};
        std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> _ringbufferSink;
        static inline size_t _maxRingBufferSize = 128;

        // Loggers writing to the same file must share one sink instance (each rotating
        // sink tracks its size and rotates on its own file handle), so all module
        // loggers share these.
        std::shared_ptr<spdlog::sinks::sink> _consoleSink;
        std::shared_ptr<spdlog::sinks::sink> _fileSink;
        std::shared_ptr<spdlog::sinks::sink> _countingSink;
        std::atomic<uint64_t> _logEventCount {0};

        std::shared_ptr<spdlog::sinks::sink> _forwardingSink;
        std::mutex _forwarderMutex;
        LogForwarder _forwarder;
        int _forwarderThreshold = spdlog::level::warn;

        // Bumped whenever cached logger handles become stale (pause toggles, shutdown).
        static inline std::atomic<uint32_t> _cacheGeneration {0};

        void EnsureSharedSinks();

      public:
        Logger();
        ~Logger() = default;

        std::shared_ptr<spdlog::logger> Get(const char *moduleName, bool async = true);

        // Flushes all loggers and tears spdlog down. Must run before static destruction
        // begins (async loggers rely on the global thread pool being alive).
        void Shutdown();

        void SetLogForwarder(LogForwarder forwarder, int threshold = spdlog::level::warn);
        void ForwardLog(int level, const std::string &name, const std::string &message);

        // The name/folder/size settings only apply to loggers created afterwards; call
        // them before the first Get().
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
            return _loggingPaused.load(std::memory_order_relaxed);
        }

        void PauseLogging(bool state) {
            // The mutex orders the flag against Get(); the release increment publishes
            // it to the lock-free cache path, whose acquire generation load then cannot
            // pair a new generation with a stale pause flag.
            std::lock_guard lock(_creationMutex);
            _loggingPaused.store(state, std::memory_order_relaxed);
            _cacheGeneration.fetch_add(1, std::memory_order_release);
        }

        void SetMaxFileCount(size_t count) {
            _maxFileCount = count;
        }

        size_t GetMaxFileCount() const {
            return _maxFileCount;
        }

        static inline void InitRingBufferCapacity(size_t capacity) {
            _maxRingBufferSize = capacity;
        }

        std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> GetRingBuffer() const {
            return _ringbufferSink;
        }

        // Total messages emitted through any logger; cheap change signal for UI consumers.
        uint64_t GetLogEventCount() const {
            return _logEventCount.load(std::memory_order_relaxed);
        }

        void CountLogEvent() {
            _logEventCount.fetch_add(1, std::memory_order_relaxed);
        }

        static uint32_t GetCacheGeneration() {
            return _cacheGeneration.load(std::memory_order_acquire);
        }
    };

    extern Logger *GetInstance();

    extern std::shared_ptr<spdlog::logger> GetLogger(const char *name, bool async = true);
} // namespace Framework::Logging
