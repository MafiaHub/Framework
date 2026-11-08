/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <function2/function2.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace Framework::Utils {
    // Splits one loop of independent iterations across persistent worker threads and returns when
    // every iteration has run: the calling thread works too, and nothing else runs meanwhile, so the
    // iterations may read whatever the caller owns as long as they write only their own results.
    //
    // For a hot path that runs every tick, not general task scheduling (see Jobs::JobSystem). The
    // workers sleep between runs; a run hands them a counter to pull iterations from, so uneven work
    // balances itself.
    class ForkJoinPool final {
      public:
        // `workers` extra threads; 0 runs everything on the caller.
        explicit ForkJoinPool(uint32_t workers = 0);
        ~ForkJoinPool();

        ForkJoinPool(const ForkJoinPool &)            = delete;
        ForkJoinPool &operator=(const ForkJoinPool &) = delete;

        // Threads that run iterations, the caller included. Lane indices are below this.
        uint32_t Lanes() const {
            return static_cast<uint32_t>(_threads.size()) + 1;
        }

        // Calls fn(iteration, lane) for every iteration in [0, count). `lane` is the running thread's
        // index, so per-thread scratch can be indexed by it without locking. Not reentrant.
        void Run(uint32_t count, fu2::function_view<void(uint32_t, uint32_t)> fn);

      private:
        void Work(uint32_t lane);
        void Drain(uint32_t lane);

        std::vector<std::thread> _threads;
        std::mutex _mutex;
        std::condition_variable _wake;
        std::condition_variable _done;
        uint64_t _run                                     = 0;
        bool _stopping                                    = false;
        uint32_t _pending                                 = 0; // workers still inside the current run
        fu2::function_view<void(uint32_t, uint32_t)> *_fn = nullptr;
        uint32_t _count                                   = 0;
        std::atomic<uint32_t> _next {0};
    };
} // namespace Framework::Utils
