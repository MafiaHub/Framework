/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Framework::Voice {
    // Where the local player is listening from. Published by the game each frame; consumed
    // by the audio thread through an atomically swapped snapshot.
    struct ListenerTransform {
        glm::vec3 position {0.0f};
        glm::vec3 forward {0.0f, 0.0f, 1.0f};
        glm::vec3 up {0.0f, 1.0f, 0.0f};
    };

    // Per-ear linear gain for one speaker, in [0, 1].
    struct SpeakerGain {
        float left  = 0.0f;
        float right = 0.0f;
    };

    // Distance attenuation and constant-power stereo pan for one speaker relative to the
    // listener. Returns silence beyond `range`. Pure: no state, safe on the audio thread.
    SpeakerGain ComputeGain(const ListenerTransform &listener, const glm::vec3 &speakerPos, float range);

    // Accumulates `samples` mono int16 samples into an interleaved stereo float buffer,
    // applying `gain`. Adds rather than assigns so several speakers can be layered.
    // `stereoOut` must hold at least samples * 2 floats.
    void MixFrameInto(float *stereoOut, const int16_t *monoIn, uint32_t samples, SpeakerGain gain);

    // Applies `volume` and bends the result into [-1, 1]. The counterpart to MixFrameInto's
    // deliberate lack of clamping, which cannot know how many speakers will sum. Transparent
    // at normal levels rather than slicing peaks flat the way a hard clamp does.
    // `stereoOut` holds samples * 2 floats. Pure: no state, safe on the audio thread.
    void LimitStereoBuffer(float *stereoOut, uint32_t samples, float volume);
} // namespace Framework::Voice
