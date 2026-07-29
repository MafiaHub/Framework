/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "job_system.h"

#include <spdlog/spdlog.h>

#include <chrono>

namespace Framework::Jobs {
    namespace {
        Metrics::Counter *g_jobsSchedHigh    = nullptr;
        Metrics::Counter *g_jobsSchedNormal  = nullptr;
        Metrics::Counter *g_jobsExceptions   = nullptr;
        Metrics::Gauge *g_jobsQueueHigh      = nullptr;
        Metrics::Gauge *g_jobsQueueNormal    = nullptr;
        Metrics::Gauge *g_jobsActive         = nullptr;
        Metrics::Histogram *g_jobsWaitHigh   = nullptr;
        Metrics::Histogram *g_jobsWaitNormal = nullptr;
        Metrics::Histogram *g_jobsExecHigh   = nullptr;
        Metrics::Histogram *g_jobsExecNormal = nullptr;
        std::once_flag g_jobMetricsOnce;

        void EnsureJobCounters() {
            std::call_once(g_jobMetricsOnce, [] {
                auto &reg         = Metrics::Registry::Get();
                g_jobsSchedHigh   = reg.RegisterCounter("fw_jobs_scheduled_total", "Jobs scheduled", {{"priority", "high"}});
                g_jobsSchedNormal = reg.RegisterCounter("fw_jobs_scheduled_total", "Jobs scheduled", {{"priority", "normal"}});
                g_jobsExceptions  = reg.RegisterCounter("fw_jobs_exceptions_total", "Jobs that threw an uncaught exception");
                g_jobsQueueHigh   = reg.RegisterGauge("fw_jobs_queue_depth", "Jobs queued but not yet executing", {{"priority", "high"}});
                g_jobsQueueNormal = reg.RegisterGauge("fw_jobs_queue_depth", "Jobs queued but not yet executing", {{"priority", "normal"}});
                g_jobsActive      = reg.RegisterGauge("fw_jobs_active", "Jobs that have begun but not yet completed");
                g_jobsWaitHigh    = reg.RegisterHistogram("fw_jobs_queue_wait_duration_seconds", "Time from scheduling until execution begins", Metrics::Buckets::Exponential(0.00005, 4.0, 9), {{"priority", "high"}});
                g_jobsWaitNormal  = reg.RegisterHistogram("fw_jobs_queue_wait_duration_seconds", "Time from scheduling until execution begins", Metrics::Buckets::Exponential(0.00005, 4.0, 9), {{"priority", "normal"}});
                g_jobsExecHigh    = reg.RegisterHistogram("fw_jobs_execution_duration_seconds", "Job execution wall time", Metrics::Buckets::Exponential(0.00005, 4.0, 9), {{"priority", "high"}});
                g_jobsExecNormal  = reg.RegisterHistogram("fw_jobs_execution_duration_seconds", "Job execution wall time", Metrics::Buckets::Exponential(0.00005, 4.0, 9), {{"priority", "normal"}});
                g_jobsQueueHigh->Set(0.0);
                g_jobsQueueNormal->Set(0.0);
                g_jobsActive->Set(0.0);
            });
        }

        Metrics::Gauge *QueueGauge(ftl::TaskPriority priority) {
            return priority == ftl::TaskPriority::High ? g_jobsQueueHigh : g_jobsQueueNormal;
        }

        Metrics::Histogram *WaitHistogram(ftl::TaskPriority priority) {
            return priority == ftl::TaskPriority::High ? g_jobsWaitHigh : g_jobsWaitNormal;
        }

        Metrics::Histogram *ExecutionHistogram(ftl::TaskPriority priority) {
            return priority == ftl::TaskPriority::High ? g_jobsExecHigh : g_jobsExecNormal;
        }
    } // namespace

    namespace Detail {
        void RecordTaskScheduled(ftl::TaskPriority priority) noexcept {
            auto *counter = priority == ftl::TaskPriority::High ? g_jobsSchedHigh : g_jobsSchedNormal;
            if (counter) {
                counter->Inc();
            }
            if (auto *queue = QueueGauge(priority)) {
                queue->Add(1.0);
            }
        }

        void RecordTaskStarted(ftl::TaskPriority priority, std::chrono::steady_clock::time_point enqueuedAt, std::chrono::steady_clock::time_point startedAt) noexcept {
            if (auto *queue = QueueGauge(priority)) {
                queue->Add(-1.0);
            }
            if (g_jobsActive) {
                g_jobsActive->Add(1.0);
            }
            if (auto *wait = WaitHistogram(priority)) {
                wait->Observe(std::chrono::duration<double>(startedAt - enqueuedAt).count());
            }
        }

        void RecordTaskFinished(ftl::TaskPriority priority, std::chrono::steady_clock::time_point startedAt) noexcept {
            if (auto *execution = ExecutionHistogram(priority)) {
                execution->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count());
            }
            if (g_jobsActive) {
                g_jobsActive->Add(-1.0);
            }
        }

        void ReportTaskException(const std::exception &exception) {
            if (g_jobsExceptions) {
                g_jobsExceptions->Inc();
            }
            spdlog::error("Task threw exception: {}", exception.what());
        }

        void ReportUnknownTaskException() {
            if (g_jobsExceptions) {
                g_jobsExceptions->Inc();
            }
            spdlog::error("Task threw unknown exception");
        }
    } // namespace Detail

    // Helper struct to wrap fu2::function for FTL's C-style function pointer
    struct TaskWrapper {
        fu2::function<void()> func;
        ftl::TaskPriority priority = ftl::TaskPriority::Normal;
        std::chrono::steady_clock::time_point enqueuedAt;
    };

    static void TaskWrapperFunc(ftl::TaskScheduler * /*scheduler*/, void *arg) {
        auto *wrapper        = static_cast<TaskWrapper *>(arg);
        const auto startedAt = std::chrono::steady_clock::now();
        Detail::RecordTaskStarted(wrapper->priority, wrapper->enqueuedAt, startedAt);
        try {
            wrapper->func();
        }
        catch (const std::exception &e) {
            Detail::ReportTaskException(e);
        }
        catch (...) {
            Detail::ReportUnknownTaskException();
        }
        Detail::RecordTaskFinished(wrapper->priority, startedAt);
        delete wrapper;
    }

    JobSystem::JobSystem(const JobSystemConfig &config): _profilingEnabled(config.enableProfiling), _config(config) {
        _scheduler = std::make_unique<ftl::TaskScheduler>();
    }

    JobSystemError JobSystem::Init() {
        ftl::TaskSchedulerInitOptions options;
        options.FiberPoolSize  = _config.fiberPoolSize;
        options.ThreadPoolSize = _config.workerThreadCount;
        options.Behavior       = _config.emptyQueueBehavior;

        EnsureJobCounters();
        _blockingCalls      = Metrics::Registry::Get().RegisterCounter("fw_jobs_blocking_calls_total", "BlockingCall invocations (detached-thread pressure indicator)");
        _callbackQueueDepth = Metrics::Registry::Get().RegisterGauge("fw_jobs_callback_queue_depth", "Pending completed-callback queue depth");
        _callbackQueueDepth->Set(0.0);

        if (_config.enableProfiling) {
            // Profiling callbacks can be added here for Tracy/Remotery integration
            options.Callbacks.Context         = this;
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
            return JobSystemError::JOB_SYSTEM_INIT_FAILED;
        }

        spdlog::info("JobSystem initialized with {} worker threads and {} fibers", _scheduler->GetThreadCount(), _scheduler->GetFiberCount());
        return JobSystemError::JOB_SYSTEM_NONE;
    }

    JobSystem::~JobSystem() {
        // Process any remaining callbacks
        ProcessCompletedCallbacks();
        spdlog::info("JobSystem shutting down");
    }

    void JobSystem::Schedule(fu2::function<void()> task, ftl::TaskPriority priority) {
        auto *wrapper = new TaskWrapper {std::move(task), priority, std::chrono::steady_clock::now()};

        ftl::Task ftlTask;
        ftlTask.Function = TaskWrapperFunc;
        ftlTask.ArgData  = wrapper;

        Detail::RecordTaskScheduled(priority);
        _scheduler->AddTask(ftlTask, priority);
    }

    void JobSystem::Schedule(const std::string & /*name*/, fu2::function<void()> task, ftl::TaskPriority priority) {
        // Name can be used for profiling in the future
        // For now, just schedule normally
        Schedule(std::move(task), priority);
    }

    void JobSystem::Schedule(ftl::WaitGroup *waitGroup, fu2::function<void()> task, ftl::TaskPriority priority) {
        auto *wrapper = new TaskWrapper {std::move(task), priority, std::chrono::steady_clock::now()};

        ftl::Task ftlTask;
        ftlTask.Function = TaskWrapperFunc;
        ftlTask.ArgData  = wrapper;

        Detail::RecordTaskScheduled(priority);
        _scheduler->AddTask(ftlTask, priority, waitGroup);
    }

    void JobSystem::RecordBlockingCall() {
        if (_blockingCalls) {
            _blockingCalls->Inc();
        }
    }

    void JobSystem::ScheduleWithCallback(fu2::function<void()> task, fu2::function<void()> onSuccess, fu2::function<void(std::exception_ptr)> onError, ftl::TaskPriority priority) {
        auto wrappedTask = [this, task = std::move(task), onSuccess = std::move(onSuccess), onError = std::move(onError)]() mutable {
            std::exception_ptr exception;
            try {
                task();
            }
            catch (...) {
                exception = std::current_exception();
            }

            if (exception) {
                if (onError) {
                    QueueCallback([onError = std::move(onError), exception]() mutable {
                        onError(exception);
                    });
                }
                else {
                    try {
                        std::rethrow_exception(exception);
                    }
                    catch (const std::exception &e) {
                        spdlog::error("Task failed with unhandled exception: {}", e.what());
                    }
                    catch (...) {
                        spdlog::error("Task failed with unknown exception");
                    }
                }
            }
            else if (onSuccess) {
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
            if (_callbackQueueDepth) {
                _callbackQueueDepth->Set(0.0);
            }
        }

        while (!callbacks.empty()) {
            auto &cb = callbacks.front();
            try {
                cb.callback();
            }
            catch (const std::exception &e) {
                spdlog::error("Callback threw exception: {}", e.what());
            }
            catch (...) {
                spdlog::error("Callback threw unknown exception");
            }
            callbacks.pop();
        }
    }

    void JobSystem::QueueCallback(fu2::function<void()> callback) {
        std::scoped_lock lock(_callbackMutex);
        _completedCallbacks.push(CompletedCallback {std::move(callback)});
        if (_callbackQueueDepth) {
            _callbackQueueDepth->Set(static_cast<double>(_completedCallbacks.size()));
        }
    }

} // namespace Framework::Jobs
