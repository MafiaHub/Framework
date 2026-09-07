/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/voice_config.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Framework::Voice {
    // Where the local player is listening from. Published by the game each frame; consumed
    // by the audio thread through an atomically swapped snapshot. Right-handed, like glm:
    // the ear axis is cross(forward, up). A left-handed game publishes `right` instead.
    struct ListenerTransform {
        glm::vec3 position {0.0f};
        glm::vec3 forward {0.0f, 0.0f, -1.0f};
        glm::vec3 up {0.0f, 1.0f, 0.0f};
        glm::vec3 right {0.0f}; // zero -> derived from forward and up

        // Distance origin, when it differs from the pan origin above: a third-person game puts the
        // camera in `position` and the character here, or two touching characters sound a boom
        // apart. Zero -> use position, as with `right`.
        glm::vec3 attenuationPosition {0.0f};
    };

    // Where one audible talker is, and how far they carry. Republished every tick for every
    // speaker the client still hears, so a sink may treat it as the whole truth rather than
    // as a delta.
    struct SpeakerPlacement {
        uint64_t speaker = 0;
        glm::vec3 position {0.0f};
        float range = kDefaultProximityRange;
    };
} // namespace Framework::Voice
