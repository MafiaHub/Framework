/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "logging/logger.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace LoggerTest {
    inline std::string ReadLog(const std::filesystem::path &folder) {
        std::ifstream file(folder / "framework.log");
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }
} // namespace LoggerTest

// A log call is made from the server tick and from script, so it must cost its caller the same whatever the
// outputs are doing.
MODULE(logger, {
    using Clock = std::chrono::steady_clock;

    IT("keeps the caller going while an output is stalled, and says how many lines that cost", {
        auto *logging     = Framework::Logging::GetInstance();
        const auto folder = std::filesystem::temp_directory_path() / "framework-logger-ut";
        std::filesystem::remove_all(folder);
        logging->SetLogFolder(folder.string());
        logging->SetConsoleLevel(spdlog::level::off);
        logging->PauseLogging(false);

        // An output that takes a second over one line, as a journal or a disk that stopped taking writes does.
        std::atomic<bool> stalled {false};
        logging->SetLogForwarder(
            [&stalled](int, const std::string &, const std::string &) {
                if (!stalled.exchange(true)) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            },
            spdlog::level::warn);
        const auto logger = Framework::Logging::GetLogger("LoggerTest");
        logger->warn("Stalls the output");
        for (int i = 0; i < 5000 && !stalled.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EQUALS(stalled.load(), true);

        // More lines than the queue holds, so the queue fills while the output is stalled.
        const uint64_t droppedBefore = logging->GetDroppedLineCount();
        const auto start             = Clock::now();
        for (int i = 0; i < 15000; ++i) {
            logger->info("Line {}", i);
        }
        const double callerMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        logger->info("Last line");

        // The report follows the stall, on the output's own thread.
        std::string log;
        for (int i = 0; i < 5000 && log.find("Last line") == std::string::npos; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            logger->flush();
            log = LoggerTest::ReadLog(folder);
        }
        const uint64_t dropped = logging->GetDroppedLineCount() - droppedBefore;

        logging->SetLogForwarder(nullptr);
        logging->PauseLogging(true);

        // The stall lasts a second: a caller that waited for the queue would be held for most of it.
        LESSER(callerMs, 500.0);
        GREATER(dropped, uint64_t {1000});
        EQUALS(log.find("Last line") != std::string::npos, true);
        EQUALS(log.find(std::to_string(dropped) + " log line(s) were dropped") != std::string::npos, true);
    });
});
