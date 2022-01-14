/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Framework::Utils {
    class JobSystem final {
      public:
        using JobProc = std::function<bool()>;

        enum JobPriority : size_t { RealTime, High, Normal, Low, Idle, NumJobPriorities };

        ~JobSystem();

        bool Init(uint32_t numThreads = 4);

        bool Shutdown();

        void EnqueueJob(JobProc job, JobPriority priority = JobPriority::Normal, bool repeatOnFail = false);

        bool IsQueueEmpty(JobPriority priority);

        bool IsPendingShutdown() const {
            return _pendingShutdown;
        }

        static JobSystem *GetInstance();

      private:
        enum class JobStatus : int32_t { Invalid = -1, Pending = 0, Running = 1 };

        struct Job {
            JobProc proc;
            JobPriority priority = JobPriority::Normal;
            JobStatus status     = JobStatus::Invalid;
            bool repeatOnFail    = false;
        };

        using JobQueue = std::queue<Job>;
        std::array<JobQueue, JobPriority::NumJobPriorities> _jobs;
        std::vector<std::thread> _threads;
        std::recursive_mutex _mutex;
        std::atomic_bool _pendingShutdown = false;
        std::atomic_uint64_t _counter     = 0;

        /**
         * Don't touch or shit explodes
         */
        constexpr uint8_t GetJobPriorityFromIndex(size_t index) const {
            constexpr uint8_t priorities[] = {2, 3, 5, 7, 11};
            return priorities[index];
        }
    };
} // namespace Framework::Utils
