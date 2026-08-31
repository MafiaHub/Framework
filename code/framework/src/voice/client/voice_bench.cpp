/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice_bench.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Framework::Voice {
    namespace {
        constexpr int64_t kFrameDurationMs = kFrameSamples * 1000 / kSampleRate;
        constexpr float kTwoPi             = 6.28318530718f;
    } // namespace

    void VoiceBench::Start(const BenchConfig &config) {
        _config        = config;
        _active        = true;
        _lastTickMs    = 0; // first Advance only seeds the clock
        _accumMs       = 0;
        _pendingFrames = 0;
    }

    void VoiceBench::Stop() {
        _active        = false;
        _sweepActive   = false;
        _pendingFrames = 0;
    }

    void VoiceBench::SetPosition(const glm::vec3 &position) {
        _sweepActive = false;
        _position    = position;
    }

    void VoiceBench::Sweep(const glm::vec3 &from, const glm::vec3 &to, float seconds, bool pingPong) {
        if (seconds <= 0.0f) {
            SetPosition(to);
            return;
        }

        _sweepFrom       = from;
        _sweepTo         = to;
        _sweepDurationMs = seconds * 1000.0f;
        _sweepElapsedMs  = 0.0f;
        _sweepPingPong   = pingPong;
        _sweepActive     = true;
        _position        = from;
    }

    void VoiceBench::Advance(int64_t nowMs) {
        if (!_active) {
            return;
        }

        if (_lastTickMs == 0) {
            _lastTickMs = nowMs;
            return;
        }

        int64_t delta = std::clamp<int64_t>(nowMs - _lastTickMs, 0, kMaxCatchUpMs);
        _lastTickMs   = nowMs;

        AdvanceSweep(delta);

        _accumMs += delta;
        _pendingFrames += static_cast<int>(_accumMs / kFrameDurationMs);
        _accumMs %= kFrameDurationMs;
    }

    bool VoiceBench::NextFrame(int16_t *out) {
        if (!_active || _pendingFrames <= 0 || out == nullptr) {
            return false;
        }

        _pendingFrames--;
        GenerateFrame(out);
        return true;
    }

    void VoiceBench::AdvanceSweep(int64_t deltaMs) {
        if (!_sweepActive) {
            return;
        }

        _sweepElapsedMs += static_cast<float>(deltaMs);
        if (_sweepElapsedMs >= _sweepDurationMs) {
            if (!_sweepPingPong) {
                _sweepActive = false;
                _position    = _sweepTo;
                return;
            }

            // One tick can cross several durations when the sweep is shorter than the frame time.
            // Each crossing is one reversal, and leaving the excess would burn a lap per tick.
            const float laps = std::floor(_sweepElapsedMs / _sweepDurationMs);
            _sweepElapsedMs -= laps * _sweepDurationMs;
            if (static_cast<long long>(laps) % 2 != 0) {
                std::swap(_sweepFrom, _sweepTo);
            }
        }

        const float t = std::clamp(_sweepElapsedMs / _sweepDurationMs, 0.0f, 1.0f);
        _position     = _sweepFrom + (_sweepTo - _sweepFrom) * t;
    }

    void VoiceBench::GenerateFrame(int16_t *out) {
        const float amplitude = std::clamp(_config.amplitude, 0.0f, 1.0f);

        if (_config.signal == BenchConfig::Signal::Noise) {
            for (uint32_t i = 0; i < kFrameSamples; i++) {
                _noiseState = _noiseState * 1664525u + 1013904223u;
                out[i]      = static_cast<int16_t>(static_cast<float>(static_cast<int16_t>(_noiseState >> 16)) * amplitude);
            }
            return;
        }

        // Phase carries across frames, otherwise every 20ms boundary clicks.
        const float step = kTwoPi * _config.frequency / static_cast<float>(kSampleRate);
        for (uint32_t i = 0; i < kFrameSamples; i++) {
            out[i] = static_cast<int16_t>(std::sin(_phase) * amplitude * 32767.0f);
            _phase += step;
            if (_phase >= kTwoPi) {
                _phase -= kTwoPi;
            }
        }
    }
} // namespace Framework::Voice
