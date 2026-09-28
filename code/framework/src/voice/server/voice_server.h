/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice_router.h"

#include <mafianet/RakVoice.h>
#include <networking/rpc/voice_settings.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Framework::Networking {
    class NetworkServer;
} // namespace Framework::Networking

namespace Framework::Voice {
    // One player starting or stopping talking. Queued rather than dispatched inline: the edges
    // are found inside a packet handler, where a script handler could re-enter the relay.
    struct TalkingChange {
        uint64_t guid = 0;
        bool talking  = false;
    };

    // One player switching voice tier by their own request. Queued for the same reason as
    // TalkingChange; a tier the server set itself is not reported, since its caller knows.
    struct TierChange {
        uint64_t guid  = 0;
        VoiceTier tier = VoiceTier::Normal;
    };

    // Server half of voice: owns the routing rule and forwards frames. Deliberately never
    // initialises a codec — RakVoice is attached purely so its relay path can forward
    // payloads, which is what keeps voice off the server's CPU budget.
    class VoiceServer final {
      public:
        VoiceServer() = default;
        // Detaches the plugin if Shutdown() was not called explicitly; see the definition.
        ~VoiceServer();

        // Non-copyable, non-movable: the attached RakVoice member's address is registered with
        // RakPeer, so relocating or duplicating this object would invalidate that pointer.
        VoiceServer(const VoiceServer &)            = delete;
        VoiceServer &operator=(const VoiceServer &) = delete;

        bool Init(Networking::NetworkServer *server);
        // Safe to call more than once.
        void Shutdown();

        // Call once per server tick. Frame forwarding itself happens on packet arrival, not
        // here; kept as an explicit hook for periodic bookkeeping.
        void Update();

        VoiceRouter &GetRouter() {
            return _router;
        }

        // Server-wide audibility radius, mirrored to connected clients. <= 0 restores
        // kDefaultProximityRange.
        void SetProximityRange(float meters);

        float GetProximityRange() const {
            return _router.GetDefaultRange();
        }

        // One talker's override of the radius above, likewise mirrored. <= 0 returns them to
        // the server-wide range.
        void SetPlayerRange(uint64_t guid, float meters);

        // The radius a voice tier carries, likewise mirrored. <= 0 makes the tier carry the
        // server-wide range.
        void SetTierRange(VoiceTier tier, float meters);

        float GetTierRange(VoiceTier tier) const {
            return _router.GetTierRange(tier);
        }

        // Puts a player on a tier, as the server's decision; mirrored like the above.
        void SetPlayerTier(uint64_t guid, VoiceTier tier);

        // The range plus every override and tier in effect, for a freshly connected client.
        void SendSettingsTo(MafiaNet::RakNetGUID guid);

        // Called by the network layer for every ID_RAKVOICE_RELAY_DATA packet.
        void OnVoiceFrame(MafiaNet::Packet *packet);

        // Whether a player is emitting voice right now.
        bool IsPlayerTalking(uint64_t guid) const;

        // Moves the edges accumulated since the last call into `out`, which is cleared first.
        // Update() must have run this tick for the stops to be current.
        void DrainTalkingChanges(std::vector<TalkingChange> &out);

        // The client's own voice setting, from the VoicePreference RPC.
        void OnPlayerPreference(uint64_t guid, bool enabled);

        // The player's own tier switch, from the VoiceTierRequest RPC. One arriving sooner than
        // kTierChangeFloorMs after the last applied switch waits for it, replacing any request
        // already waiting, rather than being dropped: the client already shows it.
        void OnTierRequest(uint64_t guid, uint8_t tier);

        // Moves the tier switches accumulated since the last call into `out`, cleared first.
        void DrainTierChanges(std::vector<TierChange> &out);

        void OnPlayerDisconnect(uint64_t guid);

      private:
        // Recipient sets are cached per talker and refreshed on an interval rather than per
        // frame; see kRecipientRefreshMs.
        struct CachedRecipients {
            std::vector<MafiaNet::RakNetGUID> guids;
            int64_t computedAtMs = 0;
        };

        const std::vector<MafiaNet::RakNetGUID> &RecipientsFor(uint64_t talker);

        // Queues a start edge on the first frame after silence.
        void MarkTalking(uint64_t talker, int64_t nowMs);

        // Applies a player's tier switch unless the last one was too recent; true when it was
        // applied or needed nothing.
        bool ApplyTierRequest(uint64_t guid, VoiceTier tier, int64_t nowMs);

        // The server-wide range and the tier radii, as every client needs them.
        Networking::RPC::VoiceSettings BuildSettings() const;

        // `guid`'s override and tier, as every client needs them.
        Networking::RPC::VoiceSpeakerRange BuildPlayerRange(uint64_t guid) const;

        // A rule changed, so every cached set is suspect -- not just the talker's own, since
        // one player's change removes them from everyone else's.
        void InvalidateRecipients();

        Networking::NetworkServer *_server = nullptr;
        bool _attached                     = false;
        MafiaNet::RakVoice _voice;
        VoiceRouter _router;
        std::unordered_map<uint64_t, CachedRecipients> _cache;
        std::vector<uint64_t> _scratch;
        // Talker -> arrival time of their most recent frame. Presence is the talking flag.
        std::unordered_map<uint64_t, int64_t> _talking;
        std::vector<TalkingChange> _talkingChanges;
        // Player -> time of their last applied tier switch, and the switches still waiting on
        // it. The second is empty but for a client pressing faster than the floor.
        std::unordered_map<uint64_t, int64_t> _tierAppliedAtMs;
        std::unordered_map<uint64_t, VoiceTier> _pendingTiers;
        std::vector<TierChange> _tierChanges;
    };
} // namespace Framework::Voice
