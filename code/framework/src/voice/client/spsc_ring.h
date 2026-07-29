/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace Framework::Voice {
    // Wait-free ring for exactly one producer thread and one consumer thread. Used to carry
    // PCM across the audio-callback boundary, where allocating or locking would risk an
    // underrun. One slot is always left empty so a full buffer is distinguishable from an
    // empty one without a separate count.
    //
    // Capacity must be a power of two so the wrap is a mask rather than a modulo.
    template <typename T, size_t Capacity>
    class SpscRing final {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

      public:
        // Producer side. Returns false and writes nothing if the data would not fit.
        bool Push(const T *src, size_t count) {
            const size_t write = _write.load(std::memory_order_relaxed);
            const size_t read  = _read.load(std::memory_order_acquire);
            const size_t free  = Capacity - 1 - ((write - read) & kMask);

            if (count > free) {
                return false;
            }

            for (size_t i = 0; i < count; i++) {
                _buffer[(write + i) & kMask] = src[i];
            }

            _write.store(write + count, std::memory_order_release);
            return true;
        }

        // Consumer side. Returns false and writes nothing if fewer than `count` are buffered.
        bool Pop(T *dst, size_t count) {
            const size_t read  = _read.load(std::memory_order_relaxed);
            const size_t write = _write.load(std::memory_order_acquire);

            if (((write - read) & kMask) < count) {
                return false;
            }

            for (size_t i = 0; i < count; i++) {
                dst[i] = _buffer[(read + i) & kMask];
            }

            _read.store(read + count, std::memory_order_release);
            return true;
        }

        // Consumer side. Elements currently readable.
        size_t Available() const {
            const size_t write = _write.load(std::memory_order_acquire);
            const size_t read  = _read.load(std::memory_order_relaxed);
            return (write - read) & kMask;
        }

        // Not safe against a concurrent producer or consumer; call only when both are stopped.
        void Clear() {
            _read.store(0, std::memory_order_relaxed);
            _write.store(0, std::memory_order_relaxed);
        }

      private:
        static constexpr size_t kMask = Capacity - 1;

        std::array<T, Capacity> _buffer {};
        std::atomic<size_t> _read {0};
        std::atomic<size_t> _write {0};
    };
} // namespace Framework::Voice
