/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/voice_config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Framework::Voice {
    // Where captured voice comes from. The mirror of IVoiceSink: implement this to take the
    // microphone from a game audio engine instead of the built-in miniaudio path, so a host
    // that already owns an audio device does not open a second one.
    //
    // Start/Stop are called with the relay session rather than at init, and every method runs
    // on the main thread, from VoiceClient::Update().
    class IVoiceSource {
      public:
        virtual ~IVoiceSource() = default;

        // False when there is no usable microphone; the client then runs listen-only. Called
        // again on each session, so a source may fail once and succeed later.
        virtual bool Start() = 0;

        // Idempotent, and called even when Start() failed.
        virtual void Stop() = 0;

        virtual bool IsRunning() const = 0;

        // Pops exactly kFrameSamples of mono int16 at kSampleRate into `out`, or writes
        // nothing and returns false. Drained in a loop until it returns false, so a source
        // that has buffered several frames hands them all over in one tick.
        virtual bool ReadFrame(int16_t *out) = 0;

        // --- device choice, optional ---
        //
        // A source that cannot choose its device keeps the defaults: no devices listed, and a
        // selection that is remembered by nobody.

        // The recording devices this source could open, by the name a player picks from.
        virtual std::vector<std::string> ListDevices() const {
            return {};
        }

        // By a name ListDevices gave; empty for the system default. A name that is no longer
        // present at Start falls back to the default rather than leaving the player mute.
        // Takes effect on the next Start -- VoiceClient restarts a running source itself.
        virtual void SelectDevice(const std::string &name) {
            (void)name;
        }

        virtual std::string GetSelectedDevice() const {
            return {};
        }
    };
} // namespace Framework::Voice
