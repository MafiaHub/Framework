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
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);

        std::array<int16_t, kFrameSamples> out {};
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1000), true);
        EQUALS(out.front() > 0 && out.front() <= 100, true);
        EQUALS(out[239], static_cast<int16_t>(24000));
        EQUALS(out.back(), static_cast<int16_t>(24000));
    });

    IT("plays a short remainder and fades to silence instead of stranding it", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames + 100> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);

        std::array<int16_t, kFrameSamples> out {};
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1000);
        }
        EQUALS(buffer.Available(), size_t {100});
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1000), true);
        EQUALS(out[99], static_cast<int16_t>(24000));
        EQUALS(out[100], static_cast<int16_t>(23900));
        EQUALS(out[339], static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
        EQUALS(buffer.Available(), size_t {0});
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1000), false);

        // Re-prime before resuming; neither a partial block nor the fade is stale PCM.
        buffer.Push(input.data(), kFrameSamples, 1100);
        out.fill(1234);
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1100), false);
        EQUALS(out.front(), static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
    });

    IT("plays the end of a word that ran the buffer dry once nothing more arrives", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        std::array<int16_t, kFrameSamples> out {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Pull(out.data(), kFrameSamples, 1000);
        }
        buffer.Pull(out.data(), kFrameSamples, 1000);
        EQUALS(buffer.Available(), size_t {0});

        // The last frame of the word lands late and alone, below the depth.
        buffer.Push(input.data(), kFrameSamples, 1060);
        const int64_t depthMs = static_cast<int64_t>(buffer.GetTargetFrames()) * 20;

        // Still inside the depth: more of the word may be on its way, so keep waiting.
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 1060), false);
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 1060 + depthMs - 1), false);
        EQUALS(buffer.Available(), static_cast<size_t>(kFrameSamples));

        // Nothing for a whole depth: that is all of it, played now rather than held for the
        // next word.
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 1060 + depthMs), true);
        EQUALS(out.front() >= 0 && out.front() <= 100, true);
        EQUALS(out.back(), static_cast<int16_t>(24000));
        EQUALS(buffer.Available(), size_t {0});
    });

    IT("plays a steady stream pulled in blocks unlike its frames without repeating or skipping a sample", {
        // KCD2's FMOD mixes in 1024-sample blocks against 960-sample voice frames. A join that
        // replays or drops audio at that boundary is the "noise at the end of every word"
        // players reported; every sample here is its predecessor plus one.
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples> frame {};
        std::array<int16_t, 1024> out {};
        constexpr int kWrap = 20000;
        int nextIn          = 0;
        int64_t nowMs       = 1000;
        size_t pushed       = 0;
        size_t pulled       = 0;

        const auto pushFrame = [&]() {
            for (int16_t &sample : frame) {
                sample = static_cast<int16_t>(nextIn);
                nextIn = (nextIn + 1) % kWrap;
            }
            buffer.Push(frame.data(), kFrameSamples, nowMs);
            pushed += kFrameSamples;
        };

        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            pushFrame();
        }

        int previous = -1;
        bool joinsClean = true;
        for (int block = 0; block < 200; ++block) {
            // Keep the depth above one block, below the trim ceiling.
            while (pushed - pulled < static_cast<size_t>(kJitterBufferFrames) * kFrameSamples + out.size()) {
                pushFrame();
            }
            EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), nowMs), true);
            pulled += out.size();
            nowMs += 21;

            // The first 5ms of the very first block fade in from silence.
            const size_t first = block == 0 ? kSampleRate / 200 : 0;
            for (size_t i = first; i < out.size(); ++i) {
                if (previous >= 0 && out[i] != static_cast<int16_t>((previous + 1) % kWrap)) {
                    joinsClean = false;
                }
                previous = out[i];
            }
        }
        EQUALS(joinsClean, true);
    });

    IT("never starts a word early because the one before it ended long ago", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        std::array<int16_t, kFrameSamples> out {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);
        for (uint32_t i = 0; i < kJitterBufferFrames + 1; ++i) {
            buffer.Pull(out.data(), kFrameSamples, 1000);
        }

        // Seconds later the next word's first frame lands. Arriving just now, it is the start
        // of a word, not the end of one: wait for the depth.
        buffer.Push(input.data(), kFrameSamples, 9000);
        out.fill(1234);
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 9000), false);
        EQUALS(out.front(), static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
        EQUALS(buffer.Available(), static_cast<size_t>(kFrameSamples));

        // The rest of the word arrives in time and it plays from its start.
        buffer.Push(input.data(), kFrameSamples * (kJitterBufferFrames - 1), 9040);
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 9040), true);
        EQUALS(out.back(), static_cast<int16_t>(24000));
    });

    IT("stays silent while empty however long nothing arrives", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        std::array<int16_t, kFrameSamples> out {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);
        for (uint32_t i = 0; i < kJitterBufferFrames + 1; ++i) {
            buffer.Pull(out.data(), kFrameSamples, 1000);
        }

        out.fill(1234);
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 100000), false);
        EQUALS(out.front(), static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
    });

    IT("keeps the underrun fade continuous across tiny callbacks", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);
        buffer.Pull(input.data(), static_cast<uint32_t>(input.size()), 1000);

        std::array<int16_t, 60> out {};
        int previous = 24000;
        for (int block = 0; block < 4; ++block) {
            EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1000), true);
            for (int16_t sample : out) {
                EQUALS(std::abs(static_cast<int>(sample) - previous) <= 100, true);
                previous = sample;
            }
        }
        EQUALS(previous, 0);
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1000), false);
    });

    IT("smooths an opposite-polarity latency trim and bounds the backlog", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples> frame {};
        std::array<int16_t, kFrameSamples> out {};
        frame.fill(30000);
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1000);
        }
        buffer.Pull(out.data(), kFrameSamples, 1000);
        EQUALS(out.back(), static_cast<int16_t>(30000));

        frame.fill(-30000);
        for (uint32_t i = 0; i < kJitterBufferMaxFrames + 1; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1050);
        }
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 1050), true);
        EQUALS(out.front(), static_cast<int16_t>(29750));
        int previous = 30000;
        for (int16_t sample : out) {
            EQUALS(std::abs(static_cast<int>(sample) - previous) <= 250, true);
            previous = sample;
        }
        EQUALS(out.back(), static_cast<int16_t>(-30000));
        EQUALS(buffer.Available() <= static_cast<size_t>(buffer.GetTargetFrames()) * kFrameSamples, true);
    });

    IT("crossfades a latency trim from the audio it skips, not the last sample played", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples> frame {};
        std::array<int16_t, kFrameSamples> out {};
        frame.fill(30000);
        buffer.Push(frame.data(), kFrameSamples, 1000);
        frame.fill(10000);
        buffer.Push(frame.data(), kFrameSamples, 1000);
        buffer.Push(frame.data(), kFrameSamples, 1000);
        buffer.Pull(out.data(), kFrameSamples, 1000);
        EQUALS(out.back(), static_cast<int16_t>(30000));

        frame.fill(-30000);
        for (uint32_t i = 0; i < kJitterBufferMaxFrames + 1; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1050);
        }
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 1050), true);
        EQUALS(out.front(), static_cast<int16_t>(9833));
        EQUALS(out.back(), static_cast<int16_t>(-30000));
    });

    IT("keeps a whole request of headroom when trimming for a large callback", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples> frame {};
        std::array<int16_t, kFrameSamples * 4> out {};
        frame.fill(24000);
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1000);
        }
        buffer.Pull(out.data(), kFrameSamples, 1000);
        for (uint32_t i = 0; i < kJitterBufferMaxFrames + 1; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1050);
        }

        // Four frames is deeper than the three-frame target the trim skips back to.
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1050), true);
        EQUALS(out.back(), static_cast<int16_t>(24000));
    });

    IT("plays what is buffered of a callback larger than it, then fades", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);

        std::array<int16_t, 20000> out {};
        out.fill(-1234);
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1000), true);
        EQUALS(out[kFrameSamples], static_cast<int16_t>(24000));
        EQUALS(out[input.size() - 1], static_cast<int16_t>(24000));
        EQUALS(out[input.size() + 239], static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
        EQUALS(buffer.Available(), size_t {0});
    });

    IT("does not leak a released speaker's fade into a new binding", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples * kJitterBufferFrames> input {};
        input.fill(24000);
        buffer.Push(input.data(), static_cast<uint32_t>(input.size()), 1000);
        buffer.Pull(input.data(), static_cast<uint32_t>(input.size()), 1000);
        buffer.Discard();
        buffer.ResetEstimate();

        std::array<int16_t, 60> out {};
        out.fill(1234);
        EQUALS(buffer.Pull(out.data(), static_cast<uint32_t>(out.size()), 1000), false);
        EQUALS(out.front(), static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
    });

    IT("fades out a full-ring overrun and restarts on fresh PCM", {
        PlayoutBuffer<32768> buffer;
        std::array<int16_t, kFrameSamples> frame {};
        std::array<int16_t, kFrameSamples> out {};
        frame.fill(24000);
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1000);
        }
        buffer.Pull(out.data(), kFrameSamples, 1000);

        // Thirty-four frames fill the ring; the thirty-fifth Push cannot fit.
        for (int i = 0; i < 33; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1050);
        }
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 1050), true);
        EQUALS(out.front(), static_cast<int16_t>(23900));
        EQUALS(out[239], static_cast<int16_t>(0));
        EQUALS(out.back(), static_cast<int16_t>(0));
        EQUALS(buffer.Available(), size_t {0});

        frame.fill(-24000);
        for (uint32_t i = 0; i < kJitterBufferFrames; ++i) {
            buffer.Push(frame.data(), kFrameSamples, 1100);
        }
        EQUALS(buffer.Pull(out.data(), kFrameSamples, 1100), true);
        EQUALS(out.front(), static_cast<int16_t>(-100));
        EQUALS(out.back(), static_cast<int16_t>(-24000));
    });
});
