/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/task_scheduler.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

MODULE(task_scheduler, {
    using Framework::Utils::TaskScheduler;

    IT("runs background work off the owner thread and its completion on it", {
        TaskScheduler scheduler(2);
        const auto owner = std::this_thread::get_id();
        std::atomic<bool> workedElsewhere {false};
        bool completedOnOwner = false;
        scheduler.RunInBackground(
            [&] {
                workedElsewhere = std::this_thread::get_id() != owner;
            },
            [&] {
                completedOnOwner = std::this_thread::get_id() == owner;
            });
        scheduler.Flush();
        EQUALS(workedElsewhere.load(), true);
        EQUALS(completedOnOwner, true);
    });

    IT("holds what is posted to the owner until the owner drains", {
        TaskScheduler scheduler(1);
        std::atomic<int> ran {0};
        std::thread poster([&] {
            scheduler.PostToOwner([&] {
                ++ran;
            });
        });
        poster.join();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        EQUALS(ran.load(), 0);
        EQUALS(scheduler.Drain(), static_cast<size_t>(1));
        EQUALS(ran.load(), 1);
        EQUALS(scheduler.Drain(), static_cast<size_t>(0));
    });

    IT("leaves a task posted during a drain for the next one", {
        TaskScheduler scheduler(0);
        int order = 0;
        int inner = 0;
        scheduler.PostToOwner([&] {
            ++order;
            scheduler.PostToOwner([&] {
                inner = ++order;
            });
        });
        EQUALS(scheduler.Drain(), static_cast<size_t>(1));
        EQUALS(inner, 0);
        EQUALS(scheduler.Drain(), static_cast<size_t>(1));
        EQUALS(inner, 2);
    });

    IT("runs every task posted from many threads exactly once", {
        TaskScheduler scheduler(4);
        constexpr int kThreads = 8;
        constexpr int kEach    = 500;
        std::vector<std::atomic<int>> hits(kThreads * kEach);
        std::vector<std::thread> posters;
        for (int t = 0; t < kThreads; ++t) {
            posters.emplace_back([&, t] {
                for (int i = 0; i < kEach; ++i) {
                    scheduler.RunInBackground(
                        [] {},
                        [&hits, index = t * kEach + i] {
                            hits[index].fetch_add(1);
                        });
                }
            });
        }
        for (auto &poster : posters) {
            poster.join();
        }
        scheduler.Flush();
        int wrong = 0;
        for (const auto &hit : hits) {
            wrong += hit.load() != 1;
        }
        EQUALS(wrong, 0);
        EQUALS(scheduler.PendingBackground(), static_cast<size_t>(0));
    });

    IT("spreads background work over its workers", {
        TaskScheduler scheduler(4);
        EQUALS(scheduler.BackgroundWorkers(), 4u);
        std::atomic<int> running {0};
        std::atomic<int> peak {0};
        for (int i = 0; i < 16; ++i) {
            scheduler.RunInBackground([&] {
                const int now = ++running;
                int seen      = peak.load();
                while (now > seen && !peak.compare_exchange_weak(seen, now)) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
                --running;
            });
        }
        scheduler.Flush();
        GREATER(peak.load(), 1);
    });

    IT("hands the result of the work to its completion", {
        TaskScheduler scheduler(1);
        std::string received;
        scheduler.Compute(
            [] {
                return std::string("built");
            },
            [&](std::string result) {
                received = std::move(result);
            });
        scheduler.Flush();
        EQUALS(received == "built", true);
    });

    IT("runs the work inline when it has no workers", {
        TaskScheduler scheduler(0);
        const auto owner = std::this_thread::get_id();
        bool inlineWork  = false;
        bool completed   = false;
        scheduler.RunInBackground(
            [&] {
                inlineWork = std::this_thread::get_id() == owner;
            },
            [&] {
                completed = true;
            });
        EQUALS(inlineWork, true);
        // The completion still waits for the drain, so callers see one order whatever the machine.
        EQUALS(completed, false);
        scheduler.Drain();
        EQUALS(completed, true);
    });

    IT("keeps working after a task throws, and skips that task's completion", {
        TaskScheduler scheduler(1);
        bool completedFailed = false;
        std::atomic<bool> ranAfter {false};
        scheduler.RunInBackground(
            [] {
                throw std::runtime_error("boom");
            },
            [&] {
                completedFailed = true;
            });
        scheduler.RunInBackground([&] {
            ranAfter = true;
        });
        scheduler.PostToOwner([] {
            throw std::runtime_error("boom on the owner");
        });
        scheduler.Flush();
        EQUALS(ranAfter.load(), true);
        EQUALS(completedFailed, false);
    });

    IT("knows which thread owns it", {
        TaskScheduler scheduler(1);
        EQUALS(scheduler.IsOwnerThread(), true);
        std::atomic<bool> elsewhere {true};
        scheduler.RunInBackground([&] {
            elsewhere = scheduler.IsOwnerThread();
        });
        scheduler.Flush();
        EQUALS(elsewhere.load(), false);
    });

    IT("finishes queued background work before it is destroyed", {
        std::atomic<int> done {0};
        {
            TaskScheduler scheduler(1);
            for (int i = 0; i < 20; ++i) {
                scheduler.RunInBackground([&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    ++done;
                });
            }
        }
        EQUALS(done.load(), 20);
    });
});
