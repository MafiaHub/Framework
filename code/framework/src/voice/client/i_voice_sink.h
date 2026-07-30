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
    // Where decoded voice ends up. Implement this to render voice through a game audio
    // engine instead of the built-in miniaudio path, without touching transport or codec.
    // Every method is called from the main thread, from VoiceClient::Update().
    class IVoiceSink {
      public:
        virtual ~IVoiceSink() = default;

        // `samples` mono int16 at kSampleRate. The buffer is reused after this returns.
        virtual void Submit(uint64_t speaker, const int16_t *mono, uint32_t samples) = 0;

        // Speaker fell silent, lost its slot, or disconnected. May name a speaker that was
        // never submitted, and may be called more than once for the same speaker.
        virtual void ReleaseSpeaker(uint64_t speaker) = 0;
    };
} // namespace Framework::Voice
