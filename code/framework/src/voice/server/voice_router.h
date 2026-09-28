/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/voice_config.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Framework::Voice {
    // Decides who hears a given talker. Deliberately free of networking and audio: it is a
    // pure function of positions, ranges and mute state, so the routing rule can be tested
    // without standing up a server. VoiceServer owns one of these and feeds it positions.
    //
    // Proximity is evaluated with a linear scan rather than through the replication interest
    // grid: InterestGrid::QueryRadius is private and keyed on NetworkEntity rather than
    // player GUIDs, and at realistic player counts the scan is cheaper than that coupling.
    class VoiceRouter final {
      public:
        // Audibility radius for talkers with no override. <= 0 restores
        // kDefaultProximityRange. Prefer VoiceServer::SetProximityRange, which also tells
        // clients, whose mixers must attenuate over the same distance this culls on.
        void SetDefaultRange(float meters);

        float GetDefaultRange() const {
            return _defaultRange;
        }

        // Upserts a player's world position. Also registers a previously unknown player.
        void SetPlayerPosition(uint64_t guid, const glm::vec3 &pos);

        // Drops all state for a player: position, range, mute flags, and every local-mute
        // entry naming them, so a reused GUID cannot inherit a stale mute.
        void RemovePlayer(uint64_t guid);

        // Overrides the audibility radius for one talker, whatever tier they chose.
        // Pass a value <= 0 to fall back to their tier.
        void SetPlayerRange(uint64_t guid, float meters);

        // The override as set, not the effective radius: 0 means "no override".
        float GetPlayerRange(uint64_t guid) const;

        // The radius a tier carries. <= 0 makes it carry the default range, which is what
        // Normal does unless a server says otherwise. Count is ignored.
        void SetTierRange(VoiceTier tier, float meters);

        // The tier's radius with the default already resolved.
        float GetTierRange(VoiceTier tier) const;

        // Whether the tier carries a radius of its own rather than the default range.
        bool HasOwnTierRange(VoiceTier tier) const;

        // The tier a player chose. Normal until they choose; Count is ignored.
        void SetPlayerTier(uint64_t guid, VoiceTier tier);

        VoiceTier GetPlayerTier(uint64_t guid) const;

        // The radius `guid` is actually heard over: the override, else their tier's.
        float GetEffectivePlayerRange(uint64_t guid) const;

        // What a client is told `guid` carries: the effective radius, or 0 when that is simply
        // the default range, so a later change of the default reaches them without a resend.
        float GetAdvertisedPlayerRange(uint64_t guid) const;

        // Talkers whose voice differs from the default in range or tier, for replaying the
        // rules to a late-joining client. Order is unspecified.
        std::vector<uint64_t> GetPlayersWithRangeRules() const;

        // Server-wide mute: a muted talker reaches nobody.
        void SetPlayerMuted(uint64_t guid, bool muted);

        bool IsPlayerMuted(uint64_t guid) const;

        // Listener-side mute: `listener` stops receiving `target`.
        void SetLocalMute(uint64_t listener, uint64_t target, bool muted);

        bool IsLocallyMuted(uint64_t listener, uint64_t target) const;

        // A deaf listener receives nobody.
        void SetPlayerDeaf(uint64_t guid, bool deaf);

        bool IsPlayerDeaf(uint64_t guid) const;

        // The player turned voice off themselves: they neither reach nor receive anyone.
        // Kept apart from mute/deaf so a client toggling its setting cannot clear an
        // administrative mute, nor a script's mute be read back as the player's preference.
        void SetPlayerVoiceDisabled(uint64_t guid, bool disabled);

        bool IsPlayerVoiceDisabled(uint64_t guid) const;

        // Fills `out` with the GUIDs that should receive `talker`'s frames. Clears `out`
        // first. Every eligible listener in range is returned -- there is no server-side
        // cap on fan-out, and the order is unspecified.
        void ComputeRecipients(uint64_t talker, std::vector<uint64_t> &out) const;

      private:
        struct PlayerState {
            glm::vec3 position {0.0f};
            float range        = 0.0f; // <= 0 means the tier's range
            VoiceTier tier     = VoiceTier::Normal;
            bool serverMuted   = false;
            bool deaf          = false;
            bool voiceDisabled = false;
            std::unordered_set<uint64_t> locallyMuted; // talkers this player does not hear
        };

        const PlayerState *Find(uint64_t guid) const;

        // The effective radius of one player's state, shared by the public lookups and the scan.
        float ResolveRange(const PlayerState &state) const;

        std::unordered_map<uint64_t, PlayerState> _players;
        float _defaultRange = kDefaultProximityRange;
        // Indexed by VoiceTier; <= 0 carries the default range.
        std::array<float, static_cast<size_t>(VoiceTier::Count)> _tierRanges {kDefaultWhisperRange, 0.0f, kDefaultShoutRange};
    };
} // namespace Framework::Voice
