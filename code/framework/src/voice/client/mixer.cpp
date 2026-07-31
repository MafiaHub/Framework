/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "mixer.h"

#include <algorithm>
#include <cmath>

namespace Framework::Voice {
    namespace {
        constexpr float kInt16Scale = 1.0f / 32768.0f;

        // Within this fraction of `range` a speaker is at full volume; beyond it attenuation
        // starts, and the floor keeps a speaker crossing the listener's own position from blowing
        // up the division. A fraction rather than an absolute distance so the curve is scale
        // invariant: a centimetre-scale game passes a proportionally larger range and gets the
        // same rolloff in physical terms. 1/25 leaves the default range's radius at 1.0.
        constexpr float kFullVolumeFraction = 1.0f / 25.0f;

        // How far the pan is allowed to swing. A full hard pan sounds wrong on headphones
        // for a speaker only slightly off-axis, so the effect is deliberately partial.
        constexpr float kMaxPan = 0.6f;

        // Where the limiter stops being transparent; above it sums are bent towards the
        // ceiling instead of sliced off.
        constexpr float kLimiterKnee = 0.75f;

        // Unity gain and C1-continuous at the knee, asymptotic to the ceiling above it, so
        // no input can exceed 1.0 and there is no audible seam where limiting begins.
        float SoftLimit(float sample) {
            const float magnitude = std::fabs(sample);
            if (magnitude <= kLimiterKnee) {
                return sample;
            }

            const float headroom = 1.0f - kLimiterKnee;
            const float excess   = (magnitude - kLimiterKnee) / headroom;
            const float limited  = kLimiterKnee + headroom * (excess / (1.0f + excess));
            return sample < 0.0f ? -limited : limited;
        }
    } // namespace

    SpeakerGain ComputeGain(const ListenerTransform &listener, const glm::vec3 &speakerPos, float range) {
        SpeakerGain gain;

        const glm::vec3 delta = speakerPos - listener.position;
        const float distance  = glm::length(delta);

        if (range <= 0.0f || distance > range) {
            return gain; // silent
        }

        // Inverse-distance rolloff, normalised so it reaches zero exactly at `range` rather
        // than trailing off asymptotically and leaving a faint always-audible tail.
        const float fullVolume  = range * kFullVolumeFraction;
        const float clamped     = std::max(distance, fullVolume);
        const float rolloff     = fullVolume / clamped;
        const float edgeFade    = 1.0f - (distance / range);
        const float attenuation = std::clamp(rolloff * edgeFade, 0.0f, 1.0f);

        // Pan on the listener's right axis. Degenerate transforms fall back to centred.
        float pan = 0.0f;
        if (distance > 0.0001f) {
            glm::vec3 right = listener.right;
            if (glm::dot(right, right) <= 0.0001f) {
                right = glm::cross(listener.forward, listener.up);
            }

            const float rightLen = glm::length(right);
            if (rightLen > 0.0001f) {
                pan = glm::dot(delta / distance, right / rightLen) * kMaxPan;
            }
        }

        // Constant-power pan: gains follow a quarter-circle so total energy stays flat as a
        // speaker sweeps across, instead of dipping in the middle as linear panning does.
        // A centred speaker therefore sits at cos(45 degrees) = ~0.707 per ear, not 1.0 —
        // that is the property that keeps perceived loudness constant, so it is not
        // normalised away.
        const float angle = (pan + 1.0f) * 0.25f * 3.14159265358979323846f;

        gain.left  = std::clamp(attenuation * std::cos(angle), 0.0f, 1.0f);
        gain.right = std::clamp(attenuation * std::sin(angle), 0.0f, 1.0f);
        return gain;
    }

    void MixFrameInto(float *stereoOut, const int16_t *monoIn, uint32_t samples, SpeakerGain gain) {
        for (uint32_t i = 0; i < samples; i++) {
            const float sample = static_cast<float>(monoIn[i]) * kInt16Scale;
            stereoOut[i * 2]     += sample * gain.left;
            stereoOut[i * 2 + 1] += sample * gain.right;
        }
    }

    void LimitStereoBuffer(float *stereoOut, uint32_t samples, float volume) {
        const size_t values = static_cast<size_t>(samples) * 2;
        for (size_t i = 0; i < values; i++) {
            stereoOut[i] = SoftLimit(stereoOut[i] * volume);
        }
    }
} // namespace Framework::Voice
