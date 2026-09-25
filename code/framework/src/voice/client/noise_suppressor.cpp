/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "noise_suppressor.h"

#include <logging/logger.h>

#include <algorithm>
#include <cmath>

// The MafiaNet archive links RNNoise into RakVoice but ships no header for it, so the four
// entry points used here are declared from RNNoise's own rnnoise.h. They are plain C and have
// not changed shape since RNNoise's first release.
extern "C" {
struct RNNModel;
DenoiseState *rnnoise_create(RNNModel *model);
void rnnoise_destroy(DenoiseState *st);
float rnnoise_process_frame(DenoiseState *st, float *out, const float *in);
int rnnoise_get_frame_size(void);
}

namespace Framework::Voice {
    static_assert(kFrameSamples % 2 == 0, "A voice frame must split into two RNNoise frames");

    NoiseSuppressor::~NoiseSuppressor() {
        Reset();
    }

    void NoiseSuppressor::Reset() {
        if (_state != nullptr) {
            rnnoise_destroy(_state);
            _state = nullptr;
        }
    }

    void NoiseSuppressor::Process(int16_t *frame) {
        if (_refused) {
            return;
        }

        if (_state == nullptr) {
            // Checked rather than assumed: a model built for another frame size would read
            // past the halves handed to it.
            if (rnnoise_get_frame_size() != static_cast<int>(kHalfSamples)) {
                _refused = true;
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: RNNoise wants {}-sample frames, not {}; noise suppression is off", rnnoise_get_frame_size(), kHalfSamples);
                return;
            }

            // Null selects the model compiled into the library.
            _state = rnnoise_create(nullptr);
            if (_state == nullptr) {
                _refused = true;
                Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: RNNoise would not start; noise suppression is off");
                return;
            }
        }

        // RNNoise works on floats at int16 scale, not on [-1, 1].
        for (uint32_t half = 0; half < 2; half++) {
            int16_t *samples = frame + half * kHalfSamples;
            for (uint32_t i = 0; i < kHalfSamples; i++) {
                _in[i] = static_cast<float>(samples[i]);
            }

            rnnoise_process_frame(_state, _out.data(), _in.data());

            for (uint32_t i = 0; i < kHalfSamples; i++) {
                samples[i] = static_cast<int16_t>(std::lrintf(std::clamp(_out[i], -32768.0f, 32767.0f)));
            }
        }
    }
} // namespace Framework::Voice
