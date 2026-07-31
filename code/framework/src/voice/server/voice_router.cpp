/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "voice_router.h"

namespace Framework::Voice {
    const VoiceRouter::PlayerState *VoiceRouter::Find(uint64_t guid) const {
        const auto it = _players.find(guid);
        return it == _players.end() ? nullptr : &it->second;
    }

    void VoiceRouter::SetDefaultRange(float meters) {
        _defaultRange = meters > 0.0f ? meters : kDefaultProximityRange;
    }

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
        _players[guid].range = meters > 0.0f ? meters : 0.0f;
    }

    float VoiceRouter::GetPlayerRange(uint64_t guid) const {
        const PlayerState *state = Find(guid);
        return state ? state->range : 0.0f;
    }

    float VoiceRouter::GetEffectivePlayerRange(uint64_t guid) const {
        const float range = GetPlayerRange(guid);
        return range > 0.0f ? range : _defaultRange;
    }

    std::vector<uint64_t> VoiceRouter::GetPlayersWithRangeOverride() const {
        std::vector<uint64_t> out;
        for (const auto &[guid, state] : _players) {
            if (state.range > 0.0f) {
                out.push_back(guid);
            }
        }

        return out;
    }

    void VoiceRouter::SetPlayerMuted(uint64_t guid, bool muted) {
        _players[guid].serverMuted = muted;
    }

    bool VoiceRouter::IsPlayerMuted(uint64_t guid) const {
        const PlayerState *state = Find(guid);
        return state && state->serverMuted;
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

    bool VoiceRouter::IsLocallyMuted(uint64_t listener, uint64_t target) const {
        const PlayerState *state = Find(listener);
        return state && state->locallyMuted.count(target) != 0;
    }

    void VoiceRouter::SetPlayerDeaf(uint64_t guid, bool deaf) {
        _players[guid].deaf = deaf;
    }

    bool VoiceRouter::IsPlayerDeaf(uint64_t guid) const {
        const PlayerState *state = Find(guid);
        return state && state->deaf;
    }

    void VoiceRouter::SetPlayerVoiceDisabled(uint64_t guid, bool disabled) {
        _players[guid].voiceDisabled = disabled;
    }

    bool VoiceRouter::IsPlayerVoiceDisabled(uint64_t guid) const {
        const PlayerState *state = Find(guid);
        return state && state->voiceDisabled;
    }

    void VoiceRouter::ComputeRecipients(uint64_t talker, std::vector<uint64_t> &out) const {
        out.clear();

        const auto talkerIt = _players.find(talker);
        if (talkerIt == _players.end() || talkerIt->second.serverMuted || talkerIt->second.voiceDisabled) {
            return;
        }

        const auto &talkerState = talkerIt->second;
        const float range       = talkerState.range > 0.0f ? talkerState.range : _defaultRange;
        const float rangeSq     = range * range;

        // Every eligible listener in range receives the frame; proximity is the only
        // fan-out bound. Order is unspecified -- callers must not read anything into it.
        out.reserve(_players.size());

        for (const auto &[guid, state] : _players) {
            if (guid == talker || state.deaf || state.voiceDisabled) {
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

            out.push_back(guid);
        }
    }
} // namespace Framework::Voice
