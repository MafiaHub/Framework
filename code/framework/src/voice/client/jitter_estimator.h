/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/voice_config.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace Framework::Voice {
    // How deep one speaker's playout buffer has to start, measured from when their audio
    // actually arrives.
    //
    // The buffer has to carry playback across the longest wait for the next batch. RakVoice
    // flushes every 50ms and the client drains it once per tick, so a speaker on a clean
    // connection arrives every 50ms and one on a jittery link in uneven clumps; a fixed depth
    // is either too deep for the first or too shallow for the second. The frames themselves
    // carry no sender timestamp this side of RakVoice, so the gap between arrivals is the
    // measurement. Frames handed over in the same millisecond are one arrival.
    //
    // A high percentile of the recent gaps rather than the largest, so one hitch does not keep
    // the latency up for the rest of the conversation, and gaps long enough to be the talker
    // pausing are left out entirely -- VAD sends nothing between words.
    //
    // Pure and single threaded: the producer side of a playout buffer owns it.
    class JitterEstimator final {
      public:
        void OnArrival(int64_t nowMs) {
            if (_lastArrivalMs == 0) {
                _lastArrivalMs = nowMs;
                return;
            }

            if (nowMs <= _lastArrivalMs) {
                return;
            }

            const int64_t gap = nowMs - _lastArrivalMs;
            _lastArrivalMs    = nowMs;
            if (gap > static_cast<int64_t>(kJitterTalkspurtGapMs)) {
                return;
            }

            _gaps[_next]  = static_cast<uint16_t>(gap);
            _next         = (_next + 1) % kJitterWindowSamples;
            _count        = std::min(_count + 1, kJitterWindowSamples);
            _targetFrames = Compute();
        }

        // A different talker now owns the slot.
        void Reset() {
            _lastArrivalMs = 0;
            _next          = 0;
            _count         = 0;
            _targetFrames  = kJitterBufferFrames;
        }

        uint32_t GetTargetFrames() const {
            return _targetFrames;
        }

      private:
        // Below this many gaps the default is a better guess than the percentile.
        static constexpr uint32_t kMinSamples = 8;

        // The percentile, in tenths.
        static constexpr uint32_t kPercentileTenths = 9;

        static constexpr uint32_t kFrameMs = (kFrameSamples * 1000) / kSampleRate;

        uint32_t Compute() const {
            if (_count < kMinSamples) {
                return kJitterBufferFrames;
            }

            std::array<uint16_t, kJitterWindowSamples> sorted = _gaps;
            const uint32_t rank                               = ((_count - 1) * kPercentileTenths) / 10;
            std::nth_element(sorted.begin(), sorted.begin() + rank, sorted.begin() + _count);

            const uint32_t gapMs  = sorted[rank];
            const uint32_t frames = (gapMs + kFrameMs - 1) / kFrameMs;
            return std::clamp(frames, kJitterBufferMinFrames, kJitterBufferMaxTargetFrames);
        }

        std::array<uint16_t, kJitterWindowSamples> _gaps {};
        uint32_t _next         = 0;
        uint32_t _count        = 0;
        int64_t _lastArrivalMs = 0;
        uint32_t _targetFrames = kJitterBufferFrames;
    };
} // namespace Framework::Voice
