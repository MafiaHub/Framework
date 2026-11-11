/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <function2/function2.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace Framework::Utils {
    // Takes blocking work off the thread that owns the world and brings its result back to it.
    //
    // The owner is the thread that constructs the scheduler: the server's tick thread. It hands work to the
    // background workers and drains, once a tick, what any thread posted back to it. Everything the owner runs it
    // runs inside Drain(), so the world still has one writer; a task that touches the world belongs in a completion
    // or in PostToOwner, never in the background half.
    //
    // For a loop of independent iterations inside a tick, see ForkJoinPool.
    //
    // Both halves are Asio's: the workers are an asio::thread_pool and the owner's queue an asio::io_context
    // it polls. This class only fixes who owns what and keeps Asio out of the headers a project sees.
    class TaskScheduler final {
      public:
        using Task = fu2::unique_function<void()>;

        // `backgroundWorkers` threads; 0 runs background work inline, on whichever thread hands it over.
        explicit TaskScheduler(uint32_t backgroundWorkers);
        // Finishes the background work already queued, then joins. Completions not drained by then never run.
        ~TaskScheduler();

        TaskScheduler(const TaskScheduler &)            = delete;
        TaskScheduler &operator=(const TaskScheduler &) = delete;

        uint32_t BackgroundWorkers() const {
            return _backgroundWorkers;
        }

        bool IsOwnerThread() const {
            return std::this_thread::get_id() == _owner;
        }

        // --- Any thread ---

        // Runs `work` on a background worker. A task that throws is logged and dropped.
        void RunInBackground(Task work);
        // Runs `work` on a background worker, then `done` on the owner during a later Drain(). `done` is skipped
        // when `work` throws.
        void RunInBackground(Task work, Task done);
        // Runs `task` on the owner during its next Drain().
        void PostToOwner(Task task);

        // `work()` on a background worker, then `done(result)` on the owner.
        template <typename Work, typename Done>
        void Compute(Work work, Done done) {
            using Result = std::invoke_result_t<Work &>;
            auto result  = std::make_shared<std::optional<Result>>();
            RunInBackground(
                [result, work = std::move(work)]() mutable {
                    result->emplace(work());
                },
                [result, done = std::move(done)]() mutable {
                    done(std::move(**result));
                });
        }

        // Background tasks queued or running.
        size_t PendingBackground() const;

        // --- Owner thread ---

        // Runs what was posted to the owner and returns how many tasks ran. A task posted while this runs waits for
        // the next call, so one drain cannot be kept going by the work it starts.
        size_t Drain();
        // Blocks until no background work is left and everything posted to the owner has run. For shutdown, and
        // for a caller that needs the result now.
        void Flush();

      private:
        struct Impl;

        std::thread::id _owner;
        uint32_t _backgroundWorkers;
        std::unique_ptr<Impl> _impl;
    };
} // namespace Framework::Utils
