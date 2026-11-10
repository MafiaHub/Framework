/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// What a log call costs the thread that makes it, with the outputs keeping up, behind, and stalled. The log lines
// go to stdout and the results to stderr: run it as `FrameworkLoggingBench <log folder> > NUL`.

#include <logging/logger.h>

#include <spdlog/async.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {
    using Clock = std::chrono::steady_clock;

    double MsSince(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    size_t Dropped() {
        const auto pool = spdlog::thread_pool();
        return pool ? pool->overrun_counter() : 0;
    }

    // Waits for the outputs to catch up, so one scenario's backlog is not charged to the next.
    double Drain() {
        const auto start = Clock::now();
        const auto pool  = spdlog::thread_pool();
        while (pool && pool->queue_size() != 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return MsSince(start);
    }

    void Run(const char *scenario, spdlog::logger &logger, int calls, std::chrono::microseconds gap) {
        const size_t droppedBefore = Dropped();
        double worstMs             = 0.0;
        double callsMs             = 0.0;
        for (int i = 0; i < calls; ++i) {
            const auto start = Clock::now();
            logger.info("Player {} moved to {:.2f} {:.2f} {:.2f} in world {}", i, 1.5 * i, 2.5 * i, 3.5 * i, i % 7);
            const double ms = MsSince(start);
            callsMs += ms;
            worstMs = std::max(worstMs, ms);
            // Spun, not slept: a thread back from a sleep runs its next call cold, which is not the call's cost.
            for (const auto until = Clock::now() + gap; Clock::now() < until;) {}
        }
        const double drainMs = Drain();
        std::fprintf(stderr, "%-34s %8d %12.0f %12.2f %12.1f %10zu %10.0f\n", scenario, calls, callsMs * 1e6 / calls, worstMs, callsMs, Dropped() - droppedBefore, drainMs);
    }
} // namespace

int main(int argc, char **argv) {
    auto *logging = Framework::Logging::GetInstance();
    logging->SetLogFolder(argc > 1 ? argv[1] : "logging-bench-logs");
    const auto logger = Framework::Logging::GetLogger("Bench");

    std::fprintf(stderr, "%-34s %8s %12s %12s %12s %10s %10s\n", "scenario", "calls", "ns per call", "worst ms", "caller ms", "dropped", "drain ms");

    // The outputs keep up: fewer lines than the queue holds, a little apart.
    Run("outputs keep up", *logger, 5000, std::chrono::microseconds(100));

    // The caller outruns the outputs: several queues' worth as fast as it can.
    Run("burst of 100k lines", *logger, 100000, std::chrono::microseconds(0));

    // An output stalls for two seconds, as a journal or a disk that stopped taking writes does, and the caller logs on.
    std::atomic<bool> stalled {false};
    logging->SetLogForwarder(
        [&stalled](int, const std::string &, const std::string &) {
            if (!stalled.exchange(true)) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        },
        spdlog::level::warn);
    logger->warn("Stalls the output");
    while (!stalled.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Run("output stalled for 2 s, 50k lines", *logger, 50000, std::chrono::microseconds(0));
    logging->SetLogForwarder(nullptr);

    logging->Shutdown();
    return 0;
}
