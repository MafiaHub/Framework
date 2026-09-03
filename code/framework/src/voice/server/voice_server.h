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

        // The range plus every override in effect, for a freshly connected client.
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
    };
} // namespace Framework::Voice
