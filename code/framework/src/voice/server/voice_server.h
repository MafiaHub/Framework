/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice_router.h"

#include <mafianet/voice.h>
#include <networking/rpc/voice_settings.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Framework::Networking {
    class NetworkServer;
} // namespace Framework::Networking

namespace Framework::Networking::Replication {
    class ReplicationManager;
} // namespace Framework::Networking::Replication

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
    //
    // Frames are relayed on RakPeer's network thread, so a stalled tick does not break anyone's
    // voice up. The router stays on the main thread: Update publishes each speaker's recipients
    // as one immutable table the relay reads, and the relay queues arrivals back for Update.
    // Everything public is main thread only.
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

        // Call once per server tick, after SyncAvatars. Takes in the frames relayed since the last
        // tick and republishes the recipients the relay forwards to.
        void Update();

        // Feeds the router each player's avatar position and virtual world. Call once per tick,
        // before that tick's packets, so its voice frames route on current worlds.
        void SyncAvatars(const Networking::Replication::ReplicationManager &replication);

        // What the relay forwards `talker`'s frames to right now: the table Update last published.
        // Rebuilt every kRecipientRefreshMs, and on the next Update after anything that decides
        // audibility other than distance changes.
        std::vector<MafiaNet::RakNetGUID> GetRelayRecipients(uint64_t talker) const;

        // The relay, on RakPeer's network thread, for every ID_RAKVOICE_RELAY_DATA packet.
        void OnVoiceFrame(MafiaNet::Packet *packet);

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
        // Moves what the relay heard into the talking set and the speakers the table covers.
        void TakeArrivals();

        // Rebuilds the relay table once it is due or stale.
        void PublishRelayTable();

        // Queues a start edge on the first frame after silence.
        void MarkTalking(uint64_t talker, int64_t nowMs);

        // Applies a player's tier switch unless the last one was too recent; true when it was
        // applied or needed nothing.
        bool ApplyTierRequest(uint64_t guid, VoiceTier tier, int64_t nowMs);

        // The server-wide range and the tier radii, as every client needs them.
        Networking::RPC::VoiceSettings BuildSettings() const;

        // `guid`'s override and tier, as every client needs them.
        Networking::RPC::VoiceSpeakerRange BuildPlayerRange(uint64_t guid) const;

        // A rule changed, so the whole table is suspect -- not just the talker's own set, since
        // one player's change removes them from everyone else's.
        void InvalidateRecipients();

        Networking::NetworkServer *_server = nullptr;
        bool _attached                     = false;
        MafiaNet::RakVoice _voice;
        VoiceRouter _router;
        std::vector<uint64_t> _scratch;
        // Every player heard this session: the relay can only forward for a talker in the table,
        // so it covers everyone who has spoken, not just who is talking now. A talker's very
        // first frame lands before they are in it and is dropped.
        std::unordered_set<uint64_t> _speakers;
        bool _relayTableDirty        = false;
        int64_t _relayTableBuiltAtMs = 0;

        // Shared with the relay on the network thread. Held only to swap a pointer or note an
        // arrival, never across a call into RakVoice.
        mutable std::mutex _relayMutex;
        // Speaker -> recipients, as the relay sees them. Replaced whole, never edited in place.
        std::shared_ptr<const std::unordered_map<uint64_t, std::vector<MafiaNet::RakNetGUID>>> _relayTable;
        // Talker -> arrival time of their latest relayed frame since the last Update.
        std::unordered_map<uint64_t, int64_t> _arrivals;
        std::unordered_map<uint64_t, int64_t> _arrivalsTaken;
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
