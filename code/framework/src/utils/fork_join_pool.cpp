/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "fork_join_pool.h"

namespace Framework::Utils {
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
        // One iteration, or nobody to share with: waking a thread costs more than it saves.
        if (_threads.empty() || count == 1) {
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
