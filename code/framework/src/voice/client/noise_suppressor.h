/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/voice_config.h"

#include <array>
#include <cstdint>

struct DenoiseState;

namespace Framework::Voice {
    // RNNoise over the microphone, before anything measures or encodes it.
    //
    // RakVoice carries RNNoise itself, but only runs it when a frame is exactly RNNoise's
    // 10ms, and voice runs at 20ms -- so it never did. Here the frame is fed through as two
    // halves instead. The model keeps state across calls, so the halves go in order and a
    // restart clears it.
    //
    // Running before the level is taken is what makes voice activation usable on a noisy
    // desk: a fan under the threshold stays under it, and one over it no longer trips the
    // gate on its own.
    //
    // Main thread only.
    class NoiseSuppressor final {
      public:
        NoiseSuppressor() = default;
        ~NoiseSuppressor();

        NoiseSuppressor(const NoiseSuppressor &)            = delete;
        NoiseSuppressor &operator=(const NoiseSuppressor &) = delete;

        // Filters one kFrameSamples frame in place. Creates the model on first use; if that
        // fails the frame passes through untouched and the failure is said once.
        void Process(int16_t *frame);

        // Frees the model, so the next Process starts from a clean state. For a new session
        // or a new microphone, where the noise the model learned is not the noise to come.
        void Reset();

        // Clears the model's history in place, without freeing it. For audio that resumes
        // after a gap the model did not see: its overlap buffers still hold the samples
        // before the gap, which would be spliced onto the first frame after it.
        void Restart();

      private:
        static constexpr uint32_t kHalfSamples = kFrameSamples / 2;

        DenoiseState *_state = nullptr;
        bool _refused        = false;
        std::array<float, kHalfSamples> _in {};
        std::array<float, kHalfSamples> _out {};
    };
} // namespace Framework::Voice
