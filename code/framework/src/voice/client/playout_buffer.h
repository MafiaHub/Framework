/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "jitter_estimator.h"
#include "spsc_ring.h"
#include "voice/voice_config.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Framework::Voice {
    // One speaker's decoded audio between the game thread that receives it and the audio
    // thread that plays it: the ring, the jitter buffer in front of playback, and the depth
    // that buffer starts at, adapted to the speaker's own connection.
    //
    // Every sink needs exactly this, and an engine sink is where it tends to be written a
    // second time with one of the three rules missing -- so it lives here, and the built-in
    // mixer uses it the same way a game's own audio engine does.
    //
    // Push and ResetEstimate are the producer thread, Pull and Discard the consumer. The only
    // thing they share besides the ring is the target depth, which is atomic.
    template <size_t RingSamples>
    class PlayoutBuffer final {
      public:
        // Producer. `nowMs` is when the audio reached the client; frames pushed in the same
        // millisecond count as one arrival. On a full ring, signal the consumer to resync;
        // the producer must never advance the consumer's read position or stall it.
        void Push(const int16_t *mono, uint32_t samples, int64_t nowMs) {
            _estimator.OnArrival(nowMs);
            _targetFrames.store(_estimator.GetTargetFrames(), std::memory_order_relaxed);
            if (!_ring.Push(mono, samples)) {
                _overflowed.store(true, std::memory_order_release);
            }
        }

        // Producer. The slot has changed hands; the next talker's connection is not the last.
        void ResetEstimate() {
            _estimator.Reset();
            _targetFrames.store(_estimator.GetTargetFrames(), std::memory_order_relaxed);
        }

        uint32_t GetTargetFrames() const {
            return _targetFrames.load(std::memory_order_relaxed);
        }

        // Consumer. Always writes exactly `samples`, including silence while priming.
        // Returns true if PCM or a fade tail was rendered. Partial and oversized device
        // requests consume what is available rather than strand it behind an all-or-nothing
        // pop. Start, underrun and latency-trim joins are smoothed over 5ms.
        bool Pull(int16_t *out, uint32_t samples) {
            if (samples == 0) {
                return false;
            }

            // The producer dropped PCM while this ring was full. Playing the old backlog
            // into the next accepted frame would join two unrelated points in the waveform.
            // Only this thread can discard safely; fade out and re-prime with fresh audio.
            if (_overflowed.exchange(false, std::memory_order_acquire)) {
                _ring.Skip(_ring.Available());
                _primed = false;
                BeginFade();
            }

            const size_t target = static_cast<size_t>(GetTargetFrames()) * kFrameSamples;
            size_t available    = _ring.Available();

            // Frames arrive in bursts, so playing the first one to land leaves the buffer
            // riding empty and every hiccup punches a hole mid-word.
            if (!_primed) {
                if (available < target) {
                    std::fill_n(out, samples, int16_t {0});
                    const bool fading = _fadeRemaining != 0;
                    ApplyFade(out, samples);
                    return fading;
                }
                _primed = true;
                BeginFade();
            }

            // Drifted deep. Late bursts and clock drift only ever add depth, so without a
            // ceiling voice falls steadily further behind. Skip once against this snapshot,
            // then blend the new waveform from the last sample actually played. Copying in a
            // loop used to make callback work depend on a concurrently refilling producer.
            const size_t ceiling = target + static_cast<size_t>(kJitterBufferMaxFrames - kJitterBufferFrames) * kFrameSamples;
            if (available > ceiling) {
                _ring.Skip(available - target);
                available = target;
                BeginFade();
            }

            const uint32_t readable = static_cast<uint32_t>(std::min<size_t>(samples, available));
            _ring.Pop(out, readable);
            ApplyFade(out, readable);
            bool rendered = readable != 0;

            if (readable < samples) {
                _primed = false;
                BeginFade();
                rendered = rendered || _fadeFrom != 0;
                std::fill_n(out + readable, samples - readable, int16_t {0});
                ApplyFade(out + readable, samples - readable);
            }

            return rendered;
        }

        // Consumer. Discards the current snapshot in constant time, remainder included.
        // A released slot must not carry the previous talker's fade into its next binding.
        void Discard() {
            _ring.Skip(_ring.Available());
            _primed        = false;
            _lastSample    = 0;
            _fadeFrom      = 0;
            _fadeRemaining = 0;
            _overflowed.store(false, std::memory_order_relaxed);
        }

        // Either side; exact only on the consumer.
        size_t Available() const {
            return _ring.Available();
        }

        // Only while neither side is running.
        void Clear() {
            _ring.Clear();
            Discard();
            ResetEstimate();
        }

      private:
        static constexpr uint32_t kFadeSamples = kSampleRate / 200; // 5ms

        void BeginFade() {
            _fadeFrom      = _lastSample;
            _fadeRemaining = kFadeSamples;
        }

        // Consumer only. A convex blend stays within PCM16, even across opposite peaks.
        // The counter spans callbacks, so a backend requesting tiny blocks gets the same
        // waveform as one requesting a whole voice frame.
        void ApplyFade(int16_t *out, uint32_t samples) {
            const uint32_t fading = std::min(samples, _fadeRemaining);
            for (uint32_t i = 0; i < fading; i++) {
                --_fadeRemaining;
                const int32_t oldWeight = static_cast<int32_t>(_fadeRemaining);
                const int32_t newWeight = static_cast<int32_t>(kFadeSamples - _fadeRemaining);
                out[i]                  = static_cast<int16_t>((static_cast<int32_t>(_fadeFrom) * oldWeight + static_cast<int32_t>(out[i]) * newWeight) / static_cast<int32_t>(kFadeSamples));
            }
            if (samples != 0) {
                _lastSample = out[samples - 1];
            }
        }

        SpscRing<int16_t, RingSamples> _ring;

        // Consumer only.
        bool _primed            = false;
        int16_t _lastSample     = 0;
        int16_t _fadeFrom       = 0;
        uint32_t _fadeRemaining = 0;

        // Producer only.
        JitterEstimator _estimator;

        std::atomic<uint32_t> _targetFrames {kJitterBufferFrames};
        std::atomic<bool> _overflowed {false};
    };
} // namespace Framework::Voice
