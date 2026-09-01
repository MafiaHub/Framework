/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/voice_config.h"

#include <cstdint>

namespace Framework::Voice {
    // Push-to-talk, held open for the release delay after the key goes up. Pressing again inside
    // that window cancels the pending release; Cut closes the gate outright. The caller supplies
    // the clock, so this is polled per tick rather than driven by a timer.
    class PushToTalkGate final {
      public:
        void SetReleaseDelay(uint32_t ms) {
            _releaseMs = ms < kMaxPushToTalkReleaseMs ? ms : kMaxPushToTalkReleaseMs;
        }

        uint32_t GetReleaseDelay() const {
            return _releaseMs;
        }

        // Edge triggered: callers poll every tick, so only the fall from held to released arms
        // the delay.
        void SetHeld(bool held, int64_t nowMs) {
            if (held) {
                _deadline = 0;
            }
            else if (_held && _releaseMs > 0) {
                _deadline = nowMs + static_cast<int64_t>(_releaseMs);
            }

            _held = held;
        }

        bool IsHeld() const {
            return _held;
        }

        // Muting, voice off, input suppression and session closure all land here.
        void Cut() {
            _held     = false;
            _deadline = 0;
        }

        bool IsOpen(int64_t nowMs) {
            if (_held || (_deadline != 0 && nowMs < _deadline)) {
                return true;
            }

            _deadline = 0;
            return false;
        }

      private:
        bool _held          = false;
        int64_t _deadline   = 0;
        uint32_t _releaseMs = kDefaultPushToTalkReleaseMs;
    };
} // namespace Framework::Voice
