/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <ftl/task_scheduler.h>
#include <ftl/wait_group.h>
#include <ftl/fibtex.h>
#include <ftl/parallel_for.h>

#include <function2.hpp>

#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace Framework::Jobs {

    /**
     * @brief Configuration options for the JobSystem
     */
    struct JobSystemConfig {
        /** Number of worker threads. 0 = auto-detect (number of cores - 1) */
        uint32_t workerThreadCount = 0;

        /** Number of fibers in the pool per thread */
        uint32_t fiberPoolSize = 400;

        /** Behavior when queues are empty: Spin, Yield, or Sleep */
        ftl::EmptyQueueBehavior emptyQueueBehavior = ftl::EmptyQueueBehavior::Sleep;

        /** Enable profiling callbacks (for Tracy/Remotery integration) */
        bool enableProfiling = false;
    };

    /**
     * @brief Result of a completed task with optional error
     */
    template <typename T>
    struct TaskResult {
        T value;
        std::string error;
        bool success = true;

        static TaskResult Success(T val) {
            return TaskResult{std::move(val), "", true};
        }

        static TaskResult Failure(std::string err) {
            return TaskResult{T{}, std::move(err), false};
        }
    };

    template <>
    struct TaskResult<void> {
        std::string error;
        bool success = true;

        static TaskResult Success() {
            return TaskResult{"", true};
        }

        static TaskResult Failure(std::string err) {
            return TaskResult{std::move(err), false};
        }
    };

    /**
     * @brief Callback wrapper for completed tasks
     */
    struct CompletedCallback {
        fu2::function<void()> callback;
    };

    /**
     * @brief Fiber-based job system built on FTL (Fiber Tasking Library)
     *
     * This class provides a high-level API for scheduling tasks on worker threads
     * using fibers for efficient context switching. Tasks can yield mid-execution
     * and resume later without blocking worker threads.
     *
     * Usage:
     * @code
     * JobSystemConfig config;
     * JobSystem jobs(config);
     *
     * // Simple task
     * jobs.Schedule([]{ doWork(); });
     *
     * // Task with dependencies
     * ftl::WaitGroup wg(jobs.GetScheduler());
     * jobs.Schedule(&wg, []{ loadTextures(); });
     * jobs.Schedule(&wg, []{ loadModels(); });
     * wg.Wait();
     * @endcode
     */
    class JobSystem final {
      public:
        /**
         * @brief Construct a new JobSystem
         * @param config Configuration options
         */
        explicit JobSystem(const JobSystemConfig &config = JobSystemConfig{});

        ~JobSystem();

        /**
         * @brief Initialize the job system scheduler
         * @return JobSystemError::JOB_SYSTEM_NONE on success
         */
        [[nodiscard]] JobSystemError Init();

        JobSystem(const JobSystem &) = delete;
        JobSystem &operator=(const JobSystem &) = delete;
        JobSystem(JobSystem &&) = delete;
        JobSystem &operator=(JobSystem &&) = delete;

        /**
         * @brief Schedule a task to run on a worker fiber
         * @param task The function to execute
         * @param priority Task priority (High or Normal)
         */
        void Schedule(fu2::function<void()> task, ftl::TaskPriority priority = ftl::TaskPriority::Normal);

        /**
         * @brief Schedule a named task (useful for profiling)
         * @param name Task name for profiling
         * @param task The function to execute
         * @param priority Task priority
         */
        void Schedule(const std::string &name, fu2::function<void()> task, ftl::TaskPriority priority = ftl::TaskPriority::Normal);

        /**
         * @brief Schedule a task with a WaitGroup for synchronization
         * @param waitGroup WaitGroup to track task completion
         * @param task The function to execute
         * @param priority Task priority
         */
        void Schedule(ftl::WaitGroup *waitGroup, fu2::function<void()> task, ftl::TaskPriority priority = ftl::TaskPriority::Normal);

        /**
         * @brief Schedule a task with success/error callbacks
         * @param task The function to execute
         * @param onSuccess Called on main thread when task succeeds
         * @param onError Called on main thread if task throws
         * @param priority Task priority
         */
        void ScheduleWithCallback(fu2::function<void()> task, fu2::function<void()> onSuccess, fu2::function<void(std::exception_ptr)> onError = nullptr, ftl::TaskPriority priority = ftl::TaskPriority::Normal);

        /**
         * @brief Schedule a batch of items to process in parallel
         * @tparam T Item type
         * @tparam Func Function type
         * @param items Vector of items to process
         * @param func Function to call for each item
         * @param batchSize Number of items per task (default: 1)
         * @param priority Task priority
         */
        template <typename T, typename Func>
        void ScheduleBatch(std::vector<T> &items, Func &&func, size_t batchSize = 1, ftl::TaskPriority priority = ftl::TaskPriority::Normal) {
            ftl::ParallelFor(_scheduler.get(), items.begin(), items.end(), batchSize, std::forward<Func>(func), priority);
        }

        /**
         * @brief Execute a blocking operation without blocking the worker thread
         *
         * The current fiber yields while the blocking operation runs on a separate thread.
         * Other tasks can run on the worker thread during this time.
         *
         * @tparam Func Function type
         * @param func The blocking function to execute
         * @return The return value of func
         */
        template <typename Func, typename ReturnType = decltype(std::declval<Func>()())>
            requires (!std::is_void_v<ReturnType>)
        ReturnType BlockingCall(Func &&func) {
            // For blocking I/O, we use a WaitGroup to yield the fiber
            ftl::WaitGroup wg(_scheduler.get());
            wg.Add(1);

            ReturnType result;
            std::exception_ptr exception;

            // Run the blocking call in a detached thread
            std::thread([&]() {
                try {
                    result = func();
                } catch (...) {
                    exception = std::current_exception();
                }
                wg.Done();
            }).detach();

            // Yield the fiber until the blocking call completes
            wg.Wait();

            if (exception) {
                std::rethrow_exception(exception);
            }

            return result;
        }

        /**
         * @brief Execute a void blocking operation without blocking the worker thread
         */
        template <typename Func, typename ReturnType = decltype(std::declval<Func>()())>
            requires std::is_void_v<ReturnType>
        void BlockingCall(Func &&func) {
            // For blocking I/O, we use a WaitGroup to yield the fiber
            ftl::WaitGroup wg(_scheduler.get());
            wg.Add(1);

            std::exception_ptr exception;

            // Run the blocking call in a detached thread
            std::thread([&]() {
                try {
                    func();
                } catch (...) {
                    exception = std::current_exception();
                }
                wg.Done();
            }).detach();

            // Yield the fiber until the blocking call completes
            wg.Wait();

            if (exception) {
                std::rethrow_exception(exception);
            }
        }

        /**
         * @brief Process completed task callbacks on the main thread
         *
         * Call this from your main update loop to handle task completion callbacks.
         * Callbacks are delivered on the calling thread, ensuring thread safety.
         */
        void ProcessCompletedCallbacks();

        /**
         * @brief Get the underlying FTL TaskScheduler
         * @return Pointer to the TaskScheduler
         */
        ftl::TaskScheduler *GetScheduler() {
            return _scheduler.get();
        }

        /**
         * @brief Get the number of worker threads
         * @return Worker thread count
         */
        uint32_t GetWorkerThreadCount() const {
            return _scheduler->GetThreadCount();
        }

        /**
         * @brief Get the number of fibers in the pool
         * @return Fiber pool size
         */
        uint32_t GetFiberCount() const {
            return _scheduler->GetFiberCount();
        }

        /**
         * @brief Create a new WaitGroup for task synchronization
         * @return A unique_ptr to a new WaitGroup
         */
        std::unique_ptr<ftl::WaitGroup> CreateWaitGroup() {
            return std::make_unique<ftl::WaitGroup>(_scheduler.get());
        }

        /**
         * @brief Create a fiber-aware mutex
         * @return A unique_ptr to a new Fibtex
         */
        std::unique_ptr<ftl::Fibtex> CreateFibtex() {
            return std::make_unique<ftl::Fibtex>(_scheduler.get());
        }

      private:
        std::unique_ptr<ftl::TaskScheduler> _scheduler;

        std::mutex _callbackMutex;
        std::queue<CompletedCallback> _completedCallbacks;

        JobSystemConfig _config;
        bool _profilingEnabled = false;

        void QueueCallback(fu2::function<void()> callback);
    };

} // namespace Framework::Jobs
