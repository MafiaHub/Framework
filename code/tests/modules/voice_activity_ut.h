/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/client/jitter_estimator.h"
#include "voice/client/playout_buffer.h"
#include "voice/client/voice_activity_gate.h"

#include <array>
#include <cstdint>
#include <limits>

MODULE(voice_activity, {
    using namespace Framework::Voice;

    IT("opens on a frame above the threshold and holds after it", {
        VoiceActivityGate gate;
        gate.SetThreshold(0.1f);
        gate.SetHold(300);

        EQUALS(gate.Update(0.05f, 1000), false);
        EQUALS(gate.Update(0.2f, 1020), true);
        EQUALS(gate.Update(0.01f, 1100), true);
        EQUALS(gate.Update(0.01f, 1319), true);
        EQUALS(gate.Update(0.01f, 1320), false);
    });

    IT("restarts the hold on every loud frame", {
        VoiceActivityGate gate;
        gate.SetThreshold(0.1f);
        gate.SetHold(300);

        gate.Update(0.2f, 1000);
        gate.Update(0.2f, 1250);
        EQUALS(gate.Update(0.0f, 1500), true);
        EQUALS(gate.Update(0.0f, 1550), false);
    });

    IT("closes at once on a cut, hold or not", {
        VoiceActivityGate gate;
        gate.SetThreshold(0.1f);
        gate.Update(0.5f, 1000);
        gate.Cut();
        EQUALS(gate.IsOpen(), false);
        EQUALS(gate.Update(0.0f, 1001), false);
    });

    IT("never opens on silence, even at a zero threshold", {
        VoiceActivityGate gate;
        gate.SetThreshold(0.0f);
        EQUALS(gate.Update(0.0f, 1000), false);
    });

    IT("clamps the threshold and ignores a non-finite one", {
        VoiceActivityGate gate;
        gate.SetThreshold(2.0f);
        EQUALS(gate.GetThreshold(), 1.0f);
        gate.SetThreshold(std::numeric_limits<float>::quiet_NaN());
        EQUALS(gate.GetThreshold(), 1.0f);
    });

    IT("starts every speaker at the fixed depth until it has measured them", {
        JitterEstimator estimator;
        EQUALS(estimator.GetTargetFrames(), kJitterBufferFrames);
        estimator.OnArrival(1000);
        estimator.OnArrival(1050);
        EQUALS(estimator.GetTargetFrames(), kJitterBufferFrames);
    });

    IT("sizes a clean 50ms connection to three frames", {
        JitterEstimator estimator;
        for (int64_t t = 1000; t < 1000 + 50 * 40; t += 50) {
            estimator.OnArrival(t);
        }
        EQUALS(estimator.GetTargetFrames(), 3u);
    });

    IT("grows for a connection whose arrivals bunch up", {
        JitterEstimator estimator;
        int64_t t = 1000;
        for (int i = 0; i < 40; i++) {
            t += (i % 3 == 0) ? 150 : 25;
            estimator.OnArrival(t);
        }
        EQUALS(estimator.GetTargetFrames(), 8u);
    });

    IT("treats a long gap as the talker pausing, not as jitter", {
        JitterEstimator estimator;
        int64_t t = 1000;
        for (int i = 0; i < 40; i++) {
            t += (i % 5 == 0) ? 900 : 50;
            estimator.OnArrival(t);
        }
        EQUALS(estimator.GetTargetFrames(), 3u);
    });

    IT("counts frames handed over in one millisecond as one arrival", {
        JitterEstimator estimator;
        for (int64_t t = 1000; t < 1000 + 50 * 40; t += 50) {
            estimator.OnArrival(t);
            estimator.OnArrival(t);
            estimator.OnArrival(t);
        }
        EQUALS(estimator.GetTargetFrames(), 3u);
    });

    IT("stays inside its bounds", {
        JitterEstimator estimator;
        for (int64_t t = 1000; t < 1000 + 240 * 40; t += 240) {
            estimator.OnArrival(t);
        }
        EQUALS(estimator.GetTargetFrames(), kJitterBufferMaxTargetFrames);

        estimator.Reset();
        for (int64_t t = 1000; t < 1000 + 5 * 40; t += 5) {
            estimator.OnArrival(t);
        }
        EQUALS(estimator.GetTargetFrames(), kJitterBufferMinFrames);
    });

    IT("primes to the target, plays, and re-primes when it runs dry", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, kFrameSamples> frame {};
        frame.fill(7);
        std::array<int16_t, kFrameSamples> out {};

        buffer.Push(frame.data(), kFrameSamples, 1000);
        buffer.Push(frame.data(), kFrameSamples, 1000);
        EQUALS(buffer.Pull(out.data(), kFrameSamples), false);

        buffer.Push(frame.data(), kFrameSamples, 1000);
        EQUALS(buffer.Pull(out.data(), kFrameSamples), true);
        EQUALS(out[0], static_cast<int16_t>(7));
        EQUALS(buffer.Pull(out.data(), kFrameSamples), true);
        EQUALS(buffer.Pull(out.data(), kFrameSamples), true);
        EQUALS(buffer.Pull(out.data(), kFrameSamples), false);

        // Re-priming: one frame is not enough to start again.
        buffer.Push(frame.data(), kFrameSamples, 1050);
        EQUALS(buffer.Pull(out.data(), kFrameSamples), false);
    });

    IT("trims a buffer that drifted past its ceiling back to the target", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, kFrameSamples> frame {};
        std::array<int16_t, kFrameSamples> out {};

        for (uint32_t i = 0; i < kJitterBufferMaxFrames + 2; i++) {
            buffer.Push(frame.data(), kFrameSamples, 1000);
        }
        EQUALS(buffer.Pull(out.data(), kFrameSamples), true);
        EQUALS(buffer.Available() <= static_cast<size_t>(buffer.GetTargetFrames()) * kFrameSamples, true);
    });

    IT("discards everything, remainder included", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, 1000> odd {};
        std::array<int16_t, 480> scratch {};
        buffer.Push(odd.data(), static_cast<uint32_t>(odd.size()), 1000);
        buffer.Discard(scratch.data(), static_cast<uint32_t>(scratch.size()));
        EQUALS(buffer.Available(), static_cast<size_t>(0));
    });
});
