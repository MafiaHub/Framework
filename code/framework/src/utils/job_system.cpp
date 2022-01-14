/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "job_system.h"

#include "logging/logger.h"

#include <optick.h>

namespace Framework::Utils {
    JobSystem *JobSystem::GetInstance() {
        static JobSystem *instance = nullptr;
        if (instance == nullptr) {
            instance = new JobSystem();
            if (!instance->Init()) {
                Logging::GetLogger(FRAMEWORK_INNER_JOBS)->error("Default job system could not be initialized");
            }
        }
        return instance;
    }

    JobSystem::~JobSystem() {
        Shutdown();
    }

    bool JobSystem::Init(uint32_t numThreads) {
        _pendingShutdown = false;

        for (size_t i = 0; i < numThreads; i++) {
            std::thread worker = std::thread([=]() {
                OPTICK_THREAD("JobSystemWorker");
                while (!_pendingShutdown) {
                    OPTICK_EVENT();
                    Job jobInfo {};

                    // Wait for a job to be available
                    {
                        const std::lock_guard<std::recursive_mutex> lock(_mutex);
                        bool last_empty = false;

                        for (auto [queue, end, i] = std::tuple {_jobs.begin(), _jobs.end(), 0}; queue != end; ++queue, ++i) {
                            if (queue->empty()) {
                                last_empty = ((queue + 1) == end);
                                continue;
                            }
                            else if (!last_empty && ((_counter++ % GetJobPriorityFromIndex(i)) != 0)) {
                                continue;
                            }

                            last_empty = false;
                            jobInfo    = queue->front();
                            queue->pop();
                            Logging::GetLogger(FRAMEWORK_INNER_JOBS)->trace("Job with priority {} is now being processed", i);

                            break;
                        }
                    }

                    if (jobInfo.status == JobStatus::Invalid) {
                        std::this_thread::yield();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    else {
                        jobInfo.status = JobStatus::Running;
                        if (!jobInfo.proc()) {
                            // TODO: Improve reports
                            Logging::GetLogger(FRAMEWORK_INNER_JOBS)->warn("Job has failed its execution");

                            if (jobInfo.repeatOnFail) {
                                Logging::GetLogger(FRAMEWORK_INNER_JOBS)->debug("Job is enqueued for another attempt");
                                jobInfo.status = JobStatus::Pending;
                                _jobs[jobInfo.priority].push(jobInfo);
                            }
                        }
                        else {
                            Logging::GetLogger(FRAMEWORK_INNER_JOBS)->trace("Job is done");
                        }
                    }
                }
            });

            Logging::GetLogger(FRAMEWORK_INNER_JOBS)->trace("Job worker thread {} was spawned", i);
            worker.detach();
            _threads.push_back(std::move(worker));
        }

        Logging::GetLogger(FRAMEWORK_INNER_JOBS)->debug("Job system was initialised");

        return true;
    }

    bool JobSystem::Shutdown() {
        _pendingShutdown = true;
        for (auto &thread : _threads) {
            thread.join();
            Logging::GetLogger(FRAMEWORK_INNER_JOBS)->debug("Job worker thread was terminated");
        }
        _threads.clear();
        return true;
    }

    void JobSystem::EnqueueJob(JobProc job, JobPriority priority, bool repeatOnFail) {
        const std::lock_guard<std::recursive_mutex> lock(_mutex);
        Logging::GetLogger(FRAMEWORK_INNER_JOBS)->trace("Job with priority {} was enqueued", priority);
        _jobs[priority].push(Job {job, priority, JobStatus::Pending, repeatOnFail});
    }

    bool JobSystem::IsQueueEmpty(JobPriority priority) {
        const std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _jobs[priority].empty();
    }

} // namespace Framework::Utils
