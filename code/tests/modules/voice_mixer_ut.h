/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/client/mixer.h"

#include <cmath>

namespace {
    Framework::Voice::ListenerTransform OriginListener() {
        Framework::Voice::ListenerTransform t;
        t.position = glm::vec3(0.0f, 0.0f, 0.0f);
        t.forward  = glm::vec3(0.0f, 0.0f, 1.0f);
        t.up       = glm::vec3(0.0f, 1.0f, 0.0f);
        return t;
    }

    bool NearlyEqual(float a, float b) {
        return std::fabs(a - b) < 0.02f;
    }
} // namespace

MODULE(voice_mixer, {
    using namespace Framework::Voice;

    IT("plays a co-located speaker at equal, unattenuated volume in both ears", {
        // Constant-power panning puts a centred speaker at cos(45 degrees) per ear, which is
        // ~0.707 and not 1.0 — that is what keeps loudness flat as a speaker pans across.
        const auto gain = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 0.0f), 25.0f);
        EQUALS(NearlyEqual(gain.left, 0.707f), true);
        EQUALS(NearlyEqual(gain.right, 0.707f), true);
    });

    IT("silences a speaker beyond the range", {
        const auto gain = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 100.0f), 25.0f);
        EQUALS(gain.left, 0.0f);
        EQUALS(gain.right, 0.0f);
    });

    IT("attenuates with distance", {
        const auto near = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 5.0f), 25.0f);
        const auto far  = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 20.0f), 25.0f);
        EQUALS(near.left > far.left, true);
    });

    IT("pans a speaker on the right louder in the right ear", {
        const auto gain = ComputeGain(OriginListener(), glm::vec3(5.0f, 0.0f, 0.0f), 25.0f);
        EQUALS(gain.right > gain.left, true);
    });

    IT("pans a speaker on the left louder in the left ear", {
        const auto gain = ComputeGain(OriginListener(), glm::vec3(-5.0f, 0.0f, 0.0f), 25.0f);
        EQUALS(gain.left > gain.right, true);
    });

    IT("keeps a speaker dead ahead centred", {
        const auto gain = ComputeGain(OriginListener(), glm::vec3(0.0f, 0.0f, 5.0f), 25.0f);
        EQUALS(NearlyEqual(gain.left, gain.right), true);
    });

    IT("accumulates a frame into the stereo output", {
        float out[4]         = {0.0f, 0.0f, 0.0f, 0.0f};
        const int16_t in[2]  = {16384, -16384};
        const SpeakerGain g  = {1.0f, 0.5f};

        MixFrameInto(out, in, 2, g);

        EQUALS(NearlyEqual(out[0], 0.5f), true);   // sample 0, left
        EQUALS(NearlyEqual(out[1], 0.25f), true);  // sample 0, right
        EQUALS(NearlyEqual(out[2], -0.5f), true);  // sample 1, left
        EQUALS(NearlyEqual(out[3], -0.25f), true); // sample 1, right
    });

    IT("sums two speakers rather than replacing", {
        float out[2]        = {0.0f, 0.0f};
        const int16_t in[1] = {16384};
        const SpeakerGain g = {1.0f, 1.0f};

        MixFrameInto(out, in, 1, g);
        MixFrameInto(out, in, 1, g);

        EQUALS(NearlyEqual(out[0], 1.0f), true);
    });
});
