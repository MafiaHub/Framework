#include "logger.h"

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

            auto dummyLogger = spdlog::stdout_color_mt(suppressedLogger);
            dummyLogger->set_level(spdlog::level::off);
            _loggers.emplace(suppressedLogger, dummyLogger);
            return dummyLogger;
        }

        // TODO multithreaded needed? Race conditions from separate threads?
        if (auto logger = spdlog::get(logName)) {
            return logger;
        }

        auto consoleLogger = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleLogger->set_level(spdlog::level::debug);
        consoleLogger->set_pattern("[%H:%M:%S] [%n] [%^%l%$] %v");

        const auto fileLogName = "logs/" + _logName + ".log";
        auto fileLogger =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(fileLogName, _maxFileSize, _maxFileCount);
        fileLogger->set_level(spdlog::level::trace);

        std::vector<spdlog::sink_ptr> sinks {consoleLogger, fileLogger};
        auto spdLogger = std::make_shared<spdlog::logger>(logName, sinks.begin(), sinks.end());
        spdLogger->set_level(spdlog::level::trace);
        spdlog::register_logger(spdLogger);

        _loggers.emplace(logName, spdLogger);
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
