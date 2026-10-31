/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Framework::Voice {
    // A finished capture: the ticket it was started with and the Opus payloads it heard, oldest first.
    struct CaptureResult {
        uint64_t ticket = 0;
        std::vector<std::vector<uint8_t>> packets;
    };

    // The packets one player sends, buffered for a server script (Voice.capture). Free of networking
    // and threads, like VoiceRouter, so it can be tested without a server. VoiceServer calls it under
    // its relay lock: Tap from the network thread, everything else from the main thread.
    class VoiceCapture final {
      public:
        // Captures `guid` from now for up to `maxMs`, clamped to kMaxCaptureMs, and at most a packet
        // per frame of that. False when `guid` already has a capture running.
        bool Start(uint64_t guid, uint64_t ticket, uint32_t maxMs, int64_t nowMs);

        // Copies one packet that arrived at `nowMs` into `guid`'s capture, if there is one still
        // running with room left. A packet larger than one frame can encode is dropped.
        void Tap(uint64_t guid, int64_t nowMs, const uint8_t *data, size_t size);

        // Notes how many packets each capture holds; call where the talking state is taken.
        void Checkpoint();

        // `guid` stopped talking: ends their capture with what it held at the last Checkpoint. A
        // capture that held nothing keeps waiting, since the stop ends an utterance from before it.
        void OnTalkingStopped(uint64_t guid, std::vector<CaptureResult> &out);

        // Ends the captures past their deadline or full.
        void Expire(int64_t nowMs, std::vector<CaptureResult> &out);

        // Ends `guid`'s capture now with everything it holds.
        void Finish(uint64_t guid, std::vector<CaptureResult> &out);

        // Drops a capture without a result: nothing waits on it any more.
        void Cancel(uint64_t ticket);

        void Clear();

        bool IsCapturing(uint64_t guid) const;

        // Only the main thread adds or removes captures, so it may read this without the lock.
        bool Empty() const {
            return _captures.empty();
        }

      private:
        struct Capture {
            uint64_t ticket    = 0;
            int64_t deadlineMs = 0;
            size_t maxPackets  = 0;
            size_t heard       = 0; // packets held at the last Checkpoint
            std::vector<std::vector<uint8_t>> packets;
        };

        using Captures = std::unordered_map<uint64_t, Capture>;

        Captures::iterator End(Captures::iterator it, std::vector<CaptureResult> &out);

        Captures _captures;
    };
} // namespace Framework::Voice
