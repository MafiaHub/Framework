/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/job_system.h"

#include <chrono>

MODULE(job_system, {
    using namespace Framework::Utils;

    IT("can spawn and process 50000 jobs", {
        std::atomic<int64_t> counter = 0;
        for (size_t i = 0; i < 50000; i++) {
            JobSystem::GetInstance()->EnqueueJob([&counter]() {
                counter += 1;
                return true;
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        int64_t res = counter;
        EQUALS(res, 50000);
    });

    IT("can repeat a failed process", {
        std::atomic<int64_t> counter = 0;
        JobSystem::GetInstance()->EnqueueJob(
            [&counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (counter == 0) {
                    counter = 1;
                    return false;
                } else if (counter == 1) {
                    counter = 42;
                }
                return true;
            },
            JobSystem::JobPriority::Normal, true);

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        int64_t res = counter;
        EQUALS(res, 42);
    });

    IT("can enqueue jobs of various priorities", {
        std::atomic<int64_t> counter      = 0;
        std::atomic<int64_t> high_counter = 0;
        for (size_t i = 0; i < 100; i++) {
            JobSystem::GetInstance()->EnqueueJob([&counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                counter += 1;
                return true;
            });
        }

        for (size_t i = 0; i < 200; i++) {
            JobSystem::GetInstance()->EnqueueJob(
                [&high_counter]() {
                    high_counter += 1;
                    return true;
                },
                JobSystem::JobPriority::RealTime);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        int64_t res1 = counter;
        int64_t res2 = high_counter;
        LESSER(res1, res2);
    });
});
