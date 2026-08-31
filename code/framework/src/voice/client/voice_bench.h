/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "mixer.h"
#include "voice/voice_config.h"

#include <array>
#include <cstdint>

namespace Framework::Voice {
    // Identity of the synthetic speaker. One below UNASSIGNED_RAKNET_GUID, so it cannot collide
    // with a real peer.
    constexpr uint64_t kBenchSpeakerId = 0xFFFFFFFFFFFFFFFEULL;

    struct BenchConfig {
        enum class Signal { Tone, Noise };

        Signal signal   = Signal::Tone;
        float frequency = 440.0f; // Hz, Tone only
        float amplitude = 0.25f;  // 0..1 peak
    };

    // Diagnostic snapshot. Assembled by VoiceClient, which owns the listener and the ranges.
    struct BenchState {
        bool active   = false;
        bool sweeping = false;
        glm::vec3 position {0.0f};
        glm::vec3 listener {0.0f};
        float distance = 0.0f;
        float range    = 0.0f;
        SpeakerGain gain {};
    };

    // Signal source for single-client voice testing: a steady tone or noise at a movable world
    // position, so attenuation and panning can be exercised with no second player, microphone or
    // network. Pure harness -- it neither mixes nor sends; the caller pumps it and submits.
    class VoiceBench final {
      public:
        void Start(const BenchConfig &config);
        void Stop();

        // Cancels any sweep in progress.
        void SetPosition(const glm::vec3 &position);
        void Sweep(const glm::vec3 &from, const glm::vec3 &to, float seconds, bool pingPong);

        // Advances the sweep and queues whole frames against the wall clock. Emitting per tick
        // instead would starve or overrun the consumer at any frame rate but exactly 50fps.
        void Advance(int64_t nowMs);

        // Pulls one queued 20ms frame into `out` (kFrameSamples mono int16); false when none.
        bool NextFrame(int16_t *out);

        bool IsActive() const {
            return _active;
        }
        bool IsSweeping() const {
            return _sweepActive;
        }
        const glm::vec3 &GetPosition() const {
            return _position;
        }
        float GetSweepProgress() const {
            return _sweepDurationMs > 0.0f ? _sweepElapsedMs / _sweepDurationMs : 0.0f;
        }

      private:
        // A frame-rate hitch must not dump a burst that overruns the consumer's ring.
        static constexpr int64_t kMaxCatchUpMs = 200;

        void AdvanceSweep(int64_t deltaMs);
        void GenerateFrame(int16_t *out);

        BenchConfig _config {};
        bool _active         = false;
        glm::vec3 _position {0.0f};
        float _phase         = 0.0f;
        uint32_t _noiseState = 0x13579BDFu;
        int64_t _lastTickMs  = 0;
        int64_t _accumMs     = 0;
        int _pendingFrames   = 0;

        bool _sweepActive      = false;
        bool _sweepPingPong    = false;
        glm::vec3 _sweepFrom {0.0f};
        glm::vec3 _sweepTo {0.0f};
        float _sweepDurationMs = 0.0f;
        float _sweepElapsedMs  = 0.0f;
    };
} // namespace Framework::Voice
