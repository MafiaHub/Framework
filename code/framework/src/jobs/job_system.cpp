/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "job_system.h"

#include <spdlog/spdlog.h>

namespace Framework::Jobs {

    // Helper struct to wrap fu2::function for FTL's C-style function pointer
    struct TaskWrapper {
        fu2::function<void()> func;
    };

    static void TaskWrapperFunc(ftl::TaskScheduler * /*scheduler*/, void *arg) {
        auto *wrapper = static_cast<TaskWrapper *>(arg);
        try {
            wrapper->func();
        } catch (const std::exception &e) {
            spdlog::error("Task threw exception: {}", e.what());
        } catch (...) {
            spdlog::error("Task threw unknown exception");
        }
        delete wrapper;
    }

    JobSystem::JobSystem(const JobSystemConfig &config) : _profilingEnabled(config.enableProfiling) {
        _scheduler = std::make_unique<ftl::TaskScheduler>();

        ftl::TaskSchedulerInitOptions options;
        options.FiberPoolSize = config.fiberPoolSize;
        options.ThreadPoolSize = config.workerThreadCount;
        options.Behavior = config.emptyQueueBehavior;

        if (config.enableProfiling) {
            // Profiling callbacks can be added here for Tracy/Remotery integration
            options.Callbacks.Context = this;
            options.Callbacks.OnFiberAttached = [](void * /*context*/, unsigned /*fiberIndex*/) {
                // Hook for profiler: fiber attached to thread
            };
            options.Callbacks.OnFiberDetached = [](void * /*context*/, unsigned /*fiberIndex*/, bool /*isMidTask*/) {
                // Hook for profiler: fiber detached from thread
            };
            options.Callbacks.OnWorkerThreadStarted = [](void * /*context*/, unsigned /*threadIndex*/) {
                // Hook for profiler: worker thread started
            };
            options.Callbacks.OnWorkerThreadEnded = [](void * /*context*/, unsigned /*threadIndex*/) {
                // Hook for profiler: worker thread ended
            };
        }

        int result = _scheduler->Init(options);
        if (result != 0) {
            spdlog::error("Failed to initialize JobSystem TaskScheduler, error code: {}", result);
            throw std::runtime_error("Failed to initialize JobSystem");
        }

        spdlog::info("JobSystem initialized with {} worker threads and {} fibers", _scheduler->GetThreadCount(), _scheduler->GetFiberCount());
    }

    JobSystem::~JobSystem() {
        // Process any remaining callbacks
        ProcessCompletedCallbacks();
        spdlog::info("JobSystem shutting down");
    }

    void JobSystem::Schedule(fu2::function<void()> task, ftl::TaskPriority priority) {
        auto *wrapper = new TaskWrapper{std::move(task)};

        ftl::Task ftlTask;
        ftlTask.Function = TaskWrapperFunc;
        ftlTask.ArgData = wrapper;

        _scheduler->AddTask(ftlTask, priority);
    }

    void JobSystem::Schedule(const std::string & /*name*/, fu2::function<void()> task, ftl::TaskPriority priority) {
        // Name can be used for profiling in the future
        // For now, just schedule normally
        Schedule(std::move(task), priority);
    }

    void JobSystem::Schedule(ftl::WaitGroup *waitGroup, fu2::function<void()> task, ftl::TaskPriority priority) {
        auto *wrapper = new TaskWrapper{std::move(task)};

        ftl::Task ftlTask;
        ftlTask.Function = TaskWrapperFunc;
        ftlTask.ArgData = wrapper;

        _scheduler->AddTask(ftlTask, priority, waitGroup);
    }

    void JobSystem::ScheduleWithCallback(fu2::function<void()> task, fu2::function<void()> onSuccess, fu2::function<void(std::exception_ptr)> onError, ftl::TaskPriority priority) {
        auto wrappedTask = [this, task = std::move(task), onSuccess = std::move(onSuccess), onError = std::move(onError)]() mutable {
            std::exception_ptr exception;
            try {
                task();
            } catch (...) {
                exception = std::current_exception();
            }

            if (exception) {
                if (onError) {
                    QueueCallback([onError = std::move(onError), exception]() mutable {
                        onError(exception);
                    });
                } else {
                    try {
                        std::rethrow_exception(exception);
                    } catch (const std::exception &e) {
                        spdlog::error("Task failed with unhandled exception: {}", e.what());
                    } catch (...) {
                        spdlog::error("Task failed with unknown exception");
                    }
                }
            } else if (onSuccess) {
                QueueCallback(std::move(onSuccess));
            }
        };

        Schedule(std::move(wrappedTask), priority);
    }

    void JobSystem::ProcessCompletedCallbacks() {
        std::queue<CompletedCallback> callbacks;

        {
            std::scoped_lock lock(_callbackMutex);
            std::swap(callbacks, _completedCallbacks);
        }

        while (!callbacks.empty()) {
            auto &cb = callbacks.front();
            try {
                cb.callback();
            } catch (const std::exception &e) {
                spdlog::error("Callback threw exception: {}", e.what());
            } catch (...) {
                spdlog::error("Callback threw unknown exception");
            }
            callbacks.pop();
        }
    }

    void JobSystem::QueueCallback(fu2::function<void()> callback) {
        std::scoped_lock lock(_callbackMutex);
        _completedCallbacks.push(CompletedCallback{std::move(callback)});
    }

} // namespace Framework::Jobs
