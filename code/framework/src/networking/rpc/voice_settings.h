/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc.h"

#include <voice/voice_config.h>

#include <cstdint>

namespace Framework::Networking::RPC {
    // Server->client: the radius the router culls on, so the client mixer fades a talker out
    // where the frames stop arriving rather than at its own compile-time default.
    struct VoiceSettings {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::VoiceSettings");

        float proximityRange = 0.0f;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, proximityRange);
        }
    };

    // Server->client: how far one talker carries, keyed by peer GUID -- the radius above
    // unless a tier or an override says otherwise, in which case `range` is that radius (0 is
    // the default). `tier` is the Voice::VoiceTier they chose, for drawing it; the talker is
    // told too, which is how a tier set by the server reaches the player's own indicator.
    struct VoiceSpeakerRange {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::VoiceSpeakerRange");

        uint64_t player = 0;
        float range     = 0.0f;
        uint8_t tier    = static_cast<uint8_t>(Framework::Voice::VoiceTier::Normal);

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, player);
            bs->Serialize(write, range);
            bs->Serialize(write, tier);
        }
    };

    // Client->server: the Voice::VoiceTier the player switched to. A request: the server
    // rate-limits it and answers with a VoiceSpeakerRange to everyone, the sender included.
    struct VoiceTierRequest {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::VoiceTierRequest");

        uint8_t tier = static_cast<uint8_t>(Framework::Voice::VoiceTier::Normal);

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, tier);
        }
    };

    // Client->server: the player's own voice setting, so the router can stop paying the
    // fan-out for a client that discards every frame. A preference, deliberately separate
    // from the router's mute and deaf flags, which are the server's to set.
    struct VoicePreference {
        static constexpr const char *kIdentifier = FW_RPC_IDENTIFIER("Framework::VoicePreference");

        bool enabled = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, enabled);
        }
    };
} // namespace Framework::Networking::RPC
