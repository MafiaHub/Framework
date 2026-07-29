/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

namespace Framework::Voice {
    // Opus operates natively at 48kHz; 20ms frames are the standard trade-off between
    // latency and per-packet header overhead. Mono, because voice is positioned by the
    // client mixer rather than carried as stereo.
    constexpr uint32_t kSampleRate   = 48000;
    constexpr uint32_t kChannels     = 1;
    constexpr uint32_t kFrameSamples = 960; // 20ms at 48kHz
    constexpr uint32_t kFrameBytes   = kFrameSamples * sizeof(int16_t);

    // Target Opus bitrate. 24kbps is transparent for speech and keeps a full lobby of
    // talkers within a few hundred kbps of server egress.
    constexpr uint32_t kBitrate = 24000;

    // Per-listener cap on simultaneously audible talkers, nearest first. Without it a
    // crowded spawn point floods every client's downstream.
    constexpr uint32_t kMaxAudibleTalkers = 6;

    // Recipient sets are recomputed on this interval rather than per frame; at 50 frames
    // per second per talker, per-frame recomputation would be 50x the work for a set that
    // changes slowly.
    constexpr uint32_t kRecipientRefreshMs = 250;

    // Default proximity audibility radius, in world units.
    constexpr float kDefaultProximityRange = 25.0f;

    // A speaker's decoder is released after this long without a frame.
    constexpr uint32_t kSpeakerSilenceTimeoutMs = 2000;
} // namespace Framework::Voice
