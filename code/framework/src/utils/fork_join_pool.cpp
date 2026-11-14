/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "fork_join_pool.h"

#include <utility>

namespace Framework::Utils {
    namespace {
        // The pool whose loop this thread is inside, and the lane it runs there.
        thread_local const ForkJoinPool *tRunning = nullptr;
        thread_local uint32_t tLane               = 0;

        // Marks this thread as inside a pool's loop, and puts back what it was, however the loop ends: an
        // iteration that throws must not leave the thread looking as if it were still in there.
        class InLoop final {
          public:
            InLoop(const ForkJoinPool *pool, uint32_t lane): _pool(std::exchange(tRunning, pool)), _lane(std::exchange(tLane, lane)) {}
            ~InLoop() {
                tRunning = _pool;
                tLane    = _lane;
            }
            InLoop(const InLoop &)            = delete;
            InLoop &operator=(const InLoop &) = delete;

          private:
            const ForkJoinPool *_pool;
            uint32_t _lane;
        };
    } // namespace

    ForkJoinPool::ForkJoinPool(uint32_t workers) {
        _threads.reserve(workers);
        for (uint32_t i = 0; i < workers; ++i) {
            _threads.emplace_back([this, lane = i + 1] {
                Work(lane);
            });
        }
    }

    ForkJoinPool::~ForkJoinPool() {
        {
            std::lock_guard lock(_mutex);
            _stopping = true;
        }
        _wake.notify_all();
        for (std::thread &thread : _threads) {
            thread.join();
        }
    }

    void ForkJoinPool::Run(uint32_t count, fu2::function_view<void(uint32_t, uint32_t)> fn) {
        if (count == 0) {
            return;
        }
        // Started from inside one of this pool's own iterations: the workers are busy with the outer loop, so the
        // inner one stays on this thread, under the lane it already has.
        if (tRunning == this) {
            for (uint32_t i = 0; i < count; ++i) {
                fn(i, tLane);
            }
            return;
        }
        // One iteration, or nobody to share with: waking a thread costs more than it saves.
        if (_threads.empty() || count == 1) {
            const InLoop inLoop(this, 0);
            for (uint32_t i = 0; i < count; ++i) {
                fn(i, 0);
            }
            return;
        }
        {
            std::lock_guard lock(_mutex);
            _fn    = &fn;
            _count = count;
            _next.store(0, std::memory_order_relaxed);
            _pending = static_cast<uint32_t>(_threads.size());
            ++_run;
        }
        _wake.notify_all();
        Drain(0);
        std::unique_lock lock(_mutex);
        _done.wait(lock, [this] {
            return _pending == 0;
        });
        _fn = nullptr;
    }

    void ForkJoinPool::Drain(uint32_t lane) {
        const InLoop inLoop(this, lane);
        for (uint32_t i = _next.fetch_add(1, std::memory_order_relaxed); i < _count; i = _next.fetch_add(1, std::memory_order_relaxed)) {
            (*_fn)(i, lane);
        }
    }

    void ForkJoinPool::Work(uint32_t lane) {
        uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock lock(_mutex);
                _wake.wait(lock, [&] {
                    return _stopping || _run != seen;
                });
                if (_stopping) {
                    return;
                }
                seen = _run;
            }
            Drain(lane);
            {
                std::lock_guard lock(_mutex);
                if (--_pending == 0) {
                    _done.notify_one();
                }
            }
        }
    }
} // namespace Framework::Utils
