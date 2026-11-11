/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

// Ahead of everything else: on Windows Asio brings winsock2.h, which has to come before windows.h, and asks which
// Windows it may assume.
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0A00
#endif
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/thread_pool.hpp>

#include "task_scheduler.h"

#include <logging/logger.h>

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>

namespace Framework::Utils {
    namespace {
        // Asio ends the process on an exception that leaves a pool thread, and stops a poll on one that leaves
        // a handler: neither is what a failed task should cost the server.
        bool RunGuarded(TaskScheduler::Task &task, const char *where) {
            try {
                task();
                return true;
            }
            catch (const std::exception &ex) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("A {} task threw: {}", where, ex.what());
            }
            catch (...) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("A {} task threw", where);
            }
            return false;
        }
    } // namespace

    struct TaskScheduler::Impl {
        explicit Impl(uint32_t workers) {
            if (workers > 0) {
                pool.emplace(workers);
            }
        }

        // Handlers posted here run only inside the owner's poll_one(), which is Asio's guarantee.
        asio::io_context owner;
        // How many of them are queued, so a drain runs what was there when it started and no more.
        std::atomic<size_t> ownerQueued {0};

        // Background tasks queued or running, and the owner waiting in Flush() for that to change.
        std::mutex mutex;
        std::condition_variable event;
        size_t background = 0;

        // Last, so it is joined first: its tasks post to `owner` and count under `mutex`.
        std::optional<asio::thread_pool> pool;
    };

    TaskScheduler::TaskScheduler(uint32_t backgroundWorkers): _owner(std::this_thread::get_id()), _backgroundWorkers(backgroundWorkers), _impl(std::make_unique<Impl>(backgroundWorkers)) {}

    TaskScheduler::~TaskScheduler() {
        // join() runs what is still queued before it returns: a caller that handed work over was promised it runs.
        if (_impl->pool) {
            _impl->pool->join();
        }
    }

    void TaskScheduler::RunInBackground(Task work) {
        if (_backgroundWorkers == 0) {
            RunGuarded(work, "background");
            return;
        }
        {
            std::lock_guard lock(_impl->mutex);
            ++_impl->background;
        }
        asio::post(*_impl->pool, [impl = _impl.get(), work = std::move(work)]() mutable {
            RunGuarded(work, "background");
            // Destroyed before the count drops: what it captured may take a while to free, and Flush() returning
            // means it is gone.
            work = nullptr;
            {
                std::lock_guard lock(impl->mutex);
                --impl->background;
            }
            impl->event.notify_all();
        });
    }

    void TaskScheduler::RunInBackground(Task work, Task done) {
        RunInBackground([this, work = std::move(work), done = std::move(done)]() mutable {
            // Throws past the post: a result that was never produced has nothing to complete.
            work();
            PostToOwner(std::move(done));
        });
    }

    void TaskScheduler::PostToOwner(Task task) {
        ++_impl->ownerQueued;
        asio::post(_impl->owner, [task = std::move(task)]() mutable {
            RunGuarded(task, "tick-thread");
        });
        // Under the lock, so a Flush() between its check and its wait cannot miss this.
        { std::lock_guard lock(_impl->mutex); }
        _impl->event.notify_all();
    }

    size_t TaskScheduler::PendingBackground() const {
        std::lock_guard lock(_impl->mutex);
        return _impl->background;
    }

    size_t TaskScheduler::Drain() {
        // What is queued now; a handler posted while these run is counted after this read and waits its turn.
        const size_t queued = _impl->ownerQueued.load();
        if (queued == 0) {
            return 0;
        }
        // An io_context that ran out of handlers stays stopped until it is told there may be more.
        _impl->owner.restart();
        size_t ran = 0;
        while (ran < queued && _impl->owner.poll_one() != 0) {
            ++ran;
        }
        _impl->ownerQueued -= ran;
        return ran;
    }

    void TaskScheduler::Flush() {
        for (;;) {
            Drain();
            std::unique_lock lock(_impl->mutex);
            if (_impl->background == 0 && _impl->ownerQueued.load() == 0) {
                return;
            }
            _impl->event.wait(lock, [this] {
                return _impl->ownerQueued.load() != 0 || _impl->background == 0;
            });
        }
    }
} // namespace Framework::Utils
