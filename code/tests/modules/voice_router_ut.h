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

namespace {
    bool RouterContains(const std::vector<uint64_t> &v, uint64_t id) {
        return std::find(v.begin(), v.end(), id) != v.end();
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

        std::vector<uint64_t> out;
        router.ComputeRecipients(1, out);

        EQUALS(out.size(), static_cast<size_t>(0));
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
});
