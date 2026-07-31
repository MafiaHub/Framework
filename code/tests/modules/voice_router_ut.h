/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "voice/server/voice_router.h"

#include <algorithm>
#include <cmath>

namespace {
    bool RouterContains(const std::vector<uint64_t> &v, uint64_t id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    }

    bool NearlyEqualRange(float a, float b) {
        return std::fabs(a - b) < 0.01f;
    }
} // namespace

MODULE(voice_router, {
    using namespace Framework::Voice;

    IT("delivers to a player inside the proximity range", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(10, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(out[0], static_cast<uint64_t>(2));
    });

    IT("excludes a player outside the proximity range", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(500, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("never delivers a talker's own voice back to them", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        // A second player in range is what makes this test able to fail: with only the
        // talker registered, an empty result would also be produced by "nobody is here".
        router.SetPlayerPosition(2, glm::vec3(1, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(RouterContains(out, 2), true);
        EQUALS(RouterContains(out, 1), false);
    });

    IT("returns every listener in range with no server-side cap", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));

        // Comfortably more than kMaxAudibleTalkers, which is a client mixer slot count and
        // must not bound what the router returns.
        constexpr uint64_t kListeners = 20;
        for (uint64_t i = 2; i < 2 + kListeners; i++) {
            router.SetPlayerPosition(i, glm::vec3(static_cast<float>(i % 5), 0, 0));
        }

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(kListeners));
    });

    IT("honours a per-talker range override", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(60, 0, 0));
        router.SetPlayerRange(1, 100.0f);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
    });

    IT("drops every recipient when the talker is server-muted", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.SetPlayerMuted(1, true);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("skips a listener who locally muted the talker", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.SetPlayerPosition(3, glm::vec3(5, 0, 0));
        router.SetLocalMute(2, 1, true);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(RouterContains(out, 3), true);
    });

    IT("skips a deaf listener", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.SetPlayerDeaf(2, true);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("forgets a removed player", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.RemovePlayer(2);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("returns nothing for an unknown talker", {
        VoiceRouter router;
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(99, out);

        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("applies a configured default range to talkers with no override", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(60, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(0));

        router.SetDefaultRange(100.0f);
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(1));
    });

    IT("restores the built-in default range for a non-positive value", {
        VoiceRouter router;
        router.SetDefaultRange(100.0f);
        router.SetDefaultRange(0.0f);

        EQUALS(NearlyEqualRange(router.GetDefaultRange(), kDefaultProximityRange), true);
    });

    IT("keeps a per-talker override winning over the default range", {
        VoiceRouter router;
        router.SetDefaultRange(100.0f);
        router.SetPlayerRange(1, 10.0f);
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(50, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
        EQUALS(NearlyEqualRange(router.GetEffectivePlayerRange(1), 10.0f), true);
        EQUALS(NearlyEqualRange(router.GetEffectivePlayerRange(2), 100.0f), true);
    });

    IT("silences a player who turned voice chat off, in both directions", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.SetPlayerVoiceDisabled(2, true);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(0));

        router.ComputeRecipients(2, out);
        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("keeps the player's own setting apart from an administrative mute", {
        VoiceRouter router;
        router.SetPlayerMuted(1, true);
        router.SetPlayerVoiceDisabled(1, false);

        EQUALS(router.IsPlayerMuted(1), true);
        EQUALS(router.IsPlayerVoiceDisabled(1), false);
    });
});
