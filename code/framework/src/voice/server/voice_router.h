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
        // Upserts a player's world position. Also registers a previously unknown player.
        void SetPlayerPosition(uint64_t guid, const glm::vec3 &pos);

        // Drops all state for a player: position, range, mute flags, and every local-mute
        // entry naming them, so a reused GUID cannot inherit a stale mute.
        void RemovePlayer(uint64_t guid);

        // Overrides the audibility radius for one talker (whisper / normal / shout).
        // Pass a value <= 0 to fall back to kDefaultProximityRange.
        void SetPlayerRange(uint64_t guid, float meters);

        // Server-wide mute: a muted talker reaches nobody.
        void SetPlayerMuted(uint64_t guid, bool muted);

        // Listener-side mute: `listener` stops receiving `target`.
        void SetLocalMute(uint64_t listener, uint64_t target, bool muted);

        // A deaf listener receives nobody.
        void SetPlayerDeaf(uint64_t guid, bool deaf);

        // Fills `out` with the GUIDs that should receive `talker`'s frames. Clears `out`
        // first. Capped at kMaxAudibleTalkers, nearest first.
        void ComputeRecipients(uint64_t talker, std::vector<uint64_t> &out) const;

      private:
        struct PlayerState {
            glm::vec3 position {0.0f};
            float range      = 0.0f; // <= 0 means kDefaultProximityRange
            bool serverMuted = false;
            bool deaf        = false;
            std::unordered_set<uint64_t> locallyMuted; // talkers this player does not hear
        };

        std::unordered_map<uint64_t, PlayerState> _players;
    };
} // namespace Framework::Voice
