/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/client/playout_buffer.h"

#include <array>
#include <cstdlib>

MODULE(playout_buffer, {
    using namespace Framework::Voice;

    IT("smooths the transition from silence without changing steady PCM", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);

        std::array<int16_t, kFrameSamples> out {};
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size())), true);
        EQUALS(out.front() > 0 && out.front() <= 100, true);
        EQUALS(out[239], static_cast<int16_t>(24000));
        EQUALS(out.back(), static_cast<int16_t>(24000));
    });

    IT("plays a short remainder and fades to silence instead of stranding it", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames + 100> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);

        std::array<int16_t, kFrameSamples> out {};
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Pull(out.data(), static_cast<uint32_t>(out.size()));
        }
        EQUALS(buffer.Available(), size_t {100});
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size())), true);
        EQUALS(out[99], static_cast<int16_t>(24000));
        EQUALS(out[100], static_cast<int16_t>(23900));
        EQUALS(out[339], static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
        EQUALS(buffer.Available(), size_t {0});
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size())), false);

        // Re-prime before resuming; neither a partial block nor the fade is stale PCM.
        buffer.Push(input.data(), kFrameSamples, 1100);
        out.fill(1234);
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size())), false);
        EQUALS(out.front(), static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
    });

    IT("keeps the underrun fade continuous across tiny callbacks", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);
        buffer.Pull(input.data(), static_cast<uint32_t>(input.size()));

        std::array<int16_t, 60> out {};
        int previous = 24000;
        for (int block = 0; block < 4; ++block) {
            EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size())), true);
            for (int16_t sample : out) {
                EQUALS(std::abs(static_cast<int>(sample) - previous) <= 100, true);
                previous = sample;
            }
        }
        EQUALS(previous, 0);
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size())), false);
    });

    IT("smooths an opposite-polarity latency trim and bounds the backlog", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, kFrameSamples> frame {};
        std::array<int16_t, kFrameSamples> out {};
        frame.fill(30000);
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1000);
        }
        buffer.Pull(out.data(), kFrameSamples);
        EQUALS(out.back(), static_cast<int16_t>(30000));

        frame.fill(-30000);
        for (uint32_t i = 0; i < kJitterBufferMaxFrames + 1; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1050);
        }
        EQUALS(buffer.Pull(out.data(), kFrameSamples), true);
        EQUALS(out.front(), static_cast<int16_t>(29750));
        int previous = 30000;
        for (int16_t sample : out) {
            EQUALS(std::abs(static_cast<int>(sample) - previous) <= 250, true);
            previous = sample;
        }
        EQUALS(out.back(), static_cast<int16_t>(-30000));
        EQUALS(buffer.Available() <= static_cast<size_t>(buffer.GetTargetFrames()) * kFrameSamples, true);
    });

    IT("handles a callback larger than the ring without permanent silence", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);

        std::array<int16_t, 20000> out {};
        out.fill(-1234);
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size())), true);
        EQUALS(out[kFrameSamples], static_cast<int16_t>(24000));
        EQUALS(out[input.size() - 1], static_cast<int16_t>(24000));
        EQUALS(out[input.size() + 239], static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
        EQUALS(buffer.Available(), size_t {0});
    });

    IT("does not leak a released speaker's fade into a new binding", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);
        buffer.Pull(input.data(), static_cast<uint32_t>(input.size()));
        buffer.Discard();
        buffer.ResetEstimate();

        std::array<int16_t, 60> out {};
        out.fill(1234);
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size())), false);
        EQUALS(out.front(), static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
    });

    IT("fades out a full-ring overrun and restarts on fresh PCM", {
        PlayoutBuffer<16384> buffer;
        std::array<int16_t, kFrameSamples> frame {};
        std::array<int16_t, kFrameSamples> out {};
        frame.fill(24000);
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1000);
        }
        buffer.Pull(out.data(), kFrameSamples);

        // Seventeen frames fill the ring; the eighteenth Push cannot fit.
        for (int i = 0; i < 16; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1050);
        }
        EQUALS(buffer.Pull(out.data(), kFrameSamples), true);
        EQUALS(out.front(), static_cast<int16_t>(23900));
        EQUALS(out[239], static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
        EQUALS(buffer.Available(), size_t {0});

        frame.fill(-24000);
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1100);
        }
        EQUALS(buffer.Pull(out.data(), kFrameSamples), true);
        EQUALS(out.front(), static_cast<int16_t>(-100));
        EQUALS(out.back(), static_cast<int16_t>(-24000));
    });
});
