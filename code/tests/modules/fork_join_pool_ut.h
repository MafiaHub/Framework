/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/fork_join_pool.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <thread>
#include <vector>

MODULE(fork_join_pool, {
    using Framework::Utils::ForkJoinPool;

    IT("runs every iteration exactly once and returns only when all have run", {
        ForkJoinPool pool(3);
        EQUALS(pool.Lanes(), static_cast<uint32_t>(4));
        for (int run = 0; run < 50; ++run) {
            std::vector<std::atomic<int>> hits(257);
            std::atomic<bool> laneOutOfRange {false};
            pool.Run(static_cast<uint32_t>(hits.size()), [&](uint32_t i, uint32_t lane) {
                hits[i].fetch_add(1);
                if (lane >= pool.Lanes()) {
                    laneOutOfRange = true;
                }
            });
            for (const auto &hit : hits) {
                EQUALS(hit.load(), 1);
            }
            EQUALS(laneOutOfRange.load(), false);
        }
    });

    IT("shares the work out to the worker threads", {
        ForkJoinPool pool(3);
        std::atomic<uint32_t> lanesSeen {0};
        pool.Run(64, [&](uint32_t, uint32_t lane) {
            lanesSeen.fetch_or(1u << lane);
            // Long enough that one lane cannot take every iteration before the others wake.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
        GREATER(std::popcount(lanesSeen.load()), 1);
    });

    IT("runs on the caller alone without workers", {
        ForkJoinPool pool(0);
        EQUALS(pool.Lanes(), static_cast<uint32_t>(1));
        const auto caller = std::this_thread::get_id();
        bool elsewhere    = false;
        int count         = 0;
        pool.Run(10, [&](uint32_t, uint32_t lane) {
            elsewhere = elsewhere || lane != 0 || std::this_thread::get_id() != caller;
            ++count;
        });
        EQUALS(count, 10);
        EQUALS(elsewhere, false);
    });

    IT("runs a loop started from inside another on the thread that started it", {
        ForkJoinPool pool(3);
        std::vector<std::atomic<int>> hits(8 * 8);
        std::atomic<bool> innerLeftItsThread {false};
        pool.Run(8, [&](uint32_t outer, uint32_t outerLane) {
            const auto self = std::this_thread::get_id();
            // The inner loop keeps the lane too, so scratch indexed by lane stays one thread's.
            pool.Run(8, [&, outer, outerLane](uint32_t inner, uint32_t lane) {
                hits[outer * 8 + inner].fetch_add(1);
                if (std::this_thread::get_id() != self || lane != outerLane) {
                    innerLeftItsThread = true;
                }
            });
        });
        int wrong = 0;
        for (const auto &hit : hits) {
            wrong += hit.load() != 1;
        }
        EQUALS(wrong, 0);
        EQUALS(innerLeftItsThread.load(), false);
    });
});
