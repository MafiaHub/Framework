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
        // millisecond count as one arrival. Drops the audio if the ring is full rather than
        // stall: a ring that full means the consumer has stopped, not slowed.
        void Push(const int16_t *mono, uint32_t samples, int64_t nowMs) {
            _estimator.OnArrival(nowMs);
            _targetFrames.store(_estimator.GetTargetFrames(), std::memory_order_relaxed);
            _ring.Push(mono, samples);
        }

        // Producer. The slot has changed hands; the next talker's connection is not the last.
        void ResetEstimate() {
            _estimator.Reset();
            _targetFrames.store(_estimator.GetTargetFrames(), std::memory_order_relaxed);
        }

        uint32_t GetTargetFrames() const {
            return _targetFrames.load(std::memory_order_relaxed);
        }

        // Consumer. Writes exactly `samples` into `out` and returns true, or leaves `out`
        // alone and returns false while the buffer is priming or has run dry.
        bool Pull(int16_t *out, uint32_t samples) {
            if (samples == 0) {
                return false;
            }

            const size_t target = static_cast<size_t>(GetTargetFrames()) * kFrameSamples;

            // Frames arrive in bursts, so playing the first one to land leaves the buffer
            // riding empty and every hiccup punches a hole mid-word.
            if (!_primed) {
                if (_ring.Available() < target) {
                    return false;
                }
                _primed = true;
            }

            // Ran dry. Hold the remainder and re-prime: splicing silence into the middle of a
            // waveform is what makes an underrun sound like distortion rather than a pause.
            if (_ring.Available() < samples) {
                _primed = false;
                return false;
            }

            // Drifted deep. Late bursts and clock drift only ever add depth, so without a
            // ceiling voice falls steadily further behind: skip the oldest audio back to the
            // target, one audible skip in exchange for bounded latency. A whole request of
            // headroom is kept, so the trim cannot starve the pop that follows it.
            const size_t ceiling = target + static_cast<size_t>(kJitterBufferMaxFrames - kJitterBufferFrames) * kFrameSamples;
            if (_ring.Available() > ceiling) {
                const size_t keep = std::max(target, static_cast<size_t>(samples));
                while (_ring.Available() >= keep + samples) {
                    _ring.Pop(out, samples);
                }
            }

            return _ring.Pop(out, samples);
        }

        // Consumer. Throws everything buffered away, down to the last sample -- a remainder
        // smaller than `scratchSamples` would otherwise leave the buffer permanently
        // non-empty -- and re-primes.
        void Discard(int16_t *scratch, uint32_t scratchSamples) {
            for (size_t pending = std::min<size_t>(scratchSamples, _ring.Available()); pending != 0; pending = std::min<size_t>(scratchSamples, _ring.Available())) {
                _ring.Pop(scratch, pending);
            }
            _primed = false;
        }

        // Either side; exact only on the consumer.
        size_t Available() const {
            return _ring.Available();
        }

        // Only while neither side is running.
        void Clear() {
            _ring.Clear();
            _primed = false;
            ResetEstimate();
        }

      private:
        SpscRing<int16_t, RingSamples> _ring;

        // Consumer only.
        bool _primed = false;

        // Producer only.
        JitterEstimator _estimator;

        std::atomic<uint32_t> _targetFrames {kJitterBufferFrames};
    };
} // namespace Framework::Voice
