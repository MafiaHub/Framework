/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/voice_config.h"

#include <cmath>
#include <cstdint>

namespace Framework::Voice {
    // Voice activation: open while the microphone is louder than a threshold, and for a hold
    // after it drops back below, so the quiet between words does not close the gate mid
    // sentence. The mirror of PushToTalkGate, with the level standing in for the key; the
    // caller supplies the clock and polls once per captured frame.
    class VoiceActivityGate final {
      public:
        // Full-scale RMS in [0, 1]. Non-finite values are dropped so a settings UI reading the
        // threshold back never sees one that silences the gate forever.
        void SetThreshold(float threshold) {
            if (!std::isfinite(threshold)) {
                return;
            }
            _threshold = threshold < 0.0f ? 0.0f : (threshold > 1.0f ? 1.0f : threshold);
        }

        float GetThreshold() const {
            return _threshold;
        }

        void SetHold(uint32_t ms) {
            _holdMs = ms < kMaxVoiceActivationHoldMs ? ms : kMaxVoiceActivationHoldMs;
        }

        uint32_t GetHold() const {
            return _holdMs;
        }

        // One captured frame's level. Returns whether that frame is sent.
        bool Update(float level, int64_t nowMs) {
            if (level >= _threshold && level > 0.0f) {
                _openUntil = nowMs + static_cast<int64_t>(_holdMs);
                _open      = true;
                return true;
            }

            _open = _open && nowMs < _openUntil;
            return _open;
        }

        bool IsOpen() const {
            return _open;
        }

        // Closes at once, skipping the hold: a block or a mode change is not the end of a
        // sentence that deserves its tail.
        void Cut() {
            _open      = false;
            _openUntil = 0;
        }

      private:
        float _threshold   = kDefaultVoiceActivationThreshold;
        uint32_t _holdMs   = kDefaultVoiceActivationHoldMs;
        int64_t _openUntil = 0;
        bool _open         = false;
    };
} // namespace Framework::Voice
