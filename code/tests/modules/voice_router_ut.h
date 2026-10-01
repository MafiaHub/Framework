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

    IT("carries a whisper and a shout over their own tier ranges", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(20, 0, 0));
        router.SetPlayerPosition(3, glm::vec3(50, 0, 0));

        std::vector<uint64_t> out;
        router.SetPlayerTier(1, VoiceTier::Whisper);
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(0));

        router.SetPlayerTier(1, VoiceTier::Normal);
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(1));

        router.SetPlayerTier(1, VoiceTier::Shout);
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(2));
        EQUALS(NearlyEqualRange(router.GetEffectivePlayerRange(1), kDefaultShoutRange), true);
    });

    IT("keeps a per-talker override winning over the chosen tier", {
        VoiceRouter router;
        router.SetPlayerTier(1, VoiceTier::Shout);
        router.SetPlayerRange(1, 5.0f);

        EQUALS(NearlyEqualRange(router.GetEffectivePlayerRange(1), 5.0f), true);

        router.SetPlayerRange(1, 0.0f);
        EQUALS(NearlyEqualRange(router.GetEffectivePlayerRange(1), kDefaultShoutRange), true);
    });

    IT("lets Normal follow the default range, and needs no replay for it", {
        VoiceRouter router;
        router.SetDefaultRange(40.0f);
        router.SetPlayerTier(1, VoiceTier::Normal);

        EQUALS(NearlyEqualRange(router.GetEffectivePlayerRange(1), 40.0f), true);
        EQUALS(router.GetPlayersWithRangeRules().empty(), true);
    });

    IT("gives Normal a radius of its own when a server sets one", {
        VoiceRouter router;
        router.SetTierRange(VoiceTier::Normal, 30.0f);
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(28, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(NearlyEqualRange(router.GetTierRanges()[static_cast<size_t>(VoiceTier::Normal)], 30.0f), true);
    });

    IT("folds a tier given no range of its own back onto the default range", {
        VoiceRouter router;
        router.SetTierRange(VoiceTier::Shout, 0.0f);
        router.SetPlayerTier(1, VoiceTier::Shout);

        EQUALS(NearlyEqualRange(router.GetTierRanges()[static_cast<size_t>(VoiceTier::Shout)], 0.0f), true);
        EQUALS(NearlyEqualRange(router.GetEffectivePlayerRange(1), kDefaultProximityRange), true);
        EQUALS(router.GetPlayersWithRangeRules().size(), static_cast<size_t>(1));
    });

    IT("ignores an out-of-range tier", {
        VoiceRouter router;
        router.SetPlayerTier(1, VoiceTier::Shout);
        router.SetPlayerTier(1, VoiceTier::Count);

        EQUALS(router.GetPlayerTier(1) == VoiceTier::Shout, true);
    });

    IT("skips a listener in another virtual world, however close, in both directions", {
        // Separate maps have their own origins, so a dungeon player can stand on the
        // coordinates of an overland one. Player 3 shares the talker's world and is what
        // makes the test able to fail on "nobody hears anything".
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(1, 0, 0));
        router.SetPlayerPosition(3, glm::vec3(1, 0, 0));
        router.SetPlayerVirtualWorld(1, 7);
        router.SetPlayerVirtualWorld(2, 8);
        router.SetPlayerVirtualWorld(3, 7);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(RouterContains(out, 3), true);

        router.ComputeRecipients(2, out);
        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("puts a player with no world set in the default world", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.SetPlayerVirtualWorld(2, MafiaNet::VIRTUAL_WORLD_DEFAULT);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(router.GetPlayerVirtualWorld(1), MafiaNet::VIRTUAL_WORLD_DEFAULT);
        EQUALS(router.GetPlayerVirtualWorld(99), MafiaNet::VIRTUAL_WORLD_DEFAULT);
    });

    IT("lets a player in the global world hear and be heard in every world", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));
        router.SetPlayerVirtualWorld(1, 7);
        router.SetPlayerVirtualWorld(2, MafiaNet::VIRTUAL_WORLD_GLOBAL);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(RouterContains(out, 2), true);

        router.ComputeRecipients(2, out);
        EQUALS(RouterContains(out, 1), true);
    });

    IT("still applies range inside a shared virtual world", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(500, 0, 0));
        router.SetPlayerVirtualWorld(1, 7);
        router.SetPlayerVirtualWorld(2, 7);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("does not let a reused GUID inherit a removed player's world", {
        VoiceRouter router;
        router.SetPlayerVirtualWorld(2, 8);
        router.RemovePlayer(2);
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(out.size(), static_cast<size_t>(1));
        EQUALS(router.GetPlayerVirtualWorld(2), MafiaNet::VIRTUAL_WORLD_DEFAULT);
    });

    IT("routes nobody to or from a player who has no position yet", {
        // A client picks a tier, or a script mutes them, before their avatar exists. That must
        // not stand them at the origin of the default world, hearing whoever is there.
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerTier(2, VoiceTier::Shout);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(RouterContains(out, 2), false);

        router.ComputeRecipients(2, out);
        EQUALS(out.size(), static_cast<size_t>(0));
    });

    IT("takes a player an avatar pass did not reach out of routing until placed again", {
        VoiceRouter router;
        router.SetPlayerPosition(1, glm::vec3(0, 0, 0));
        router.SetPlayerPosition(2, glm::vec3(5, 0, 0));

        router.BeginAvatarPass();
        router.SetPlayerAvatar(1, glm::vec3(0, 0, 0), MafiaNet::VIRTUAL_WORLD_DEFAULT);
        EQUALS(router.EndAvatarPass(), true);

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);
        EQUALS(RouterContains(out, 2), false);
        router.ComputeRecipients(2, out);
        EQUALS(out.size(), static_cast<size_t>(0));

        router.BeginAvatarPass();
        router.SetPlayerAvatar(1, glm::vec3(0, 0, 0), MafiaNet::VIRTUAL_WORLD_DEFAULT);
        EQUALS(router.SetPlayerAvatar(2, glm::vec3(5, 0, 0), MafiaNet::VIRTUAL_WORLD_DEFAULT), true);
        EQUALS(router.EndAvatarPass(), false);

        router.ComputeRecipients(1, out);
        EQUALS(RouterContains(out, 2), true);
    });

    IT("reports an avatar as a change only when it places the player or changes their world", {
        // Moves ride the recipient refresh interval; only a placement or a world change has to
        // cut audio at once.
        VoiceRouter router;
        EQUALS(router.SetPlayerAvatar(1, glm::vec3(0, 0, 0), 7), true);

        EQUALS(router.SetPlayerAvatar(1, glm::vec3(3, 0, 0), 7), false);
        EQUALS(router.SetPlayerAvatar(1, glm::vec3(3, 0, 0), 8), true);
        EQUALS(router.GetPlayerVirtualWorld(1), static_cast<MafiaNet::VirtualWorldId>(8));
    });
});
