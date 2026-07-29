/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice_router.h"

#include <algorithm>

namespace Framework::Voice {
    void VoiceRouter::SetPlayerPosition(uint64_t guid, const glm::vec3 &pos) {
        _players[guid].position = pos;
    }

    void VoiceRouter::RemovePlayer(uint64_t guid) {
        _players.erase(guid);

        // A local mute naming the departed player must go too, or a recycled GUID would
        // silently inherit it.
        for (auto &entry : _players) {
            entry.second.locallyMuted.erase(guid);
        }
    }

    void VoiceRouter::SetPlayerRange(uint64_t guid, float meters) {
        _players[guid].range = meters;
    }

    void VoiceRouter::SetPlayerMuted(uint64_t guid, bool muted) {
        _players[guid].serverMuted = muted;
    }

    void VoiceRouter::SetLocalMute(uint64_t listener, uint64_t target, bool muted) {
        auto &state = _players[listener];
        if (muted) {
            state.locallyMuted.insert(target);
        }
        else {
            state.locallyMuted.erase(target);
        }
    }

    void VoiceRouter::SetPlayerDeaf(uint64_t guid, bool deaf) {
        _players[guid].deaf = deaf;
    }

    void VoiceRouter::ComputeRecipients(uint64_t talker, std::vector<uint64_t> &out) const {
        out.clear();

        const auto talkerIt = _players.find(talker);
        if (talkerIt == _players.end() || talkerIt->second.serverMuted) {
            return;
        }

        const auto &talkerState = talkerIt->second;
        const float range       = talkerState.range > 0.0f ? talkerState.range : kDefaultProximityRange;
        const float rangeSq     = range * range;

        // Collected with distance so the cap can keep the nearest listeners.
        std::vector<std::pair<float, uint64_t>> candidates;
        candidates.reserve(_players.size());

        for (const auto &[guid, state] : _players) {
            if (guid == talker || state.deaf) {
                continue;
            }
            if (state.locallyMuted.count(talker) != 0) {
                continue;
            }

            const glm::vec3 delta = state.position - talkerState.position;
            const float distSq    = glm::dot(delta, delta);
            if (distSq > rangeSq) {
                continue;
            }

            candidates.emplace_back(distSq, guid);
        }

        if (candidates.size() > kMaxAudibleTalkers) {
            std::partial_sort(candidates.begin(), candidates.begin() + kMaxAudibleTalkers, candidates.end());
            candidates.resize(kMaxAudibleTalkers);
        }

        out.reserve(candidates.size());
        for (const auto &[distSq, guid] : candidates) {
            out.push_back(guid);
        }
    }
} // namespace Framework::Voice
