/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/messages/messages.h"

#include <BitStream.h>
#include <MessageIdentifiers.h>
#include <functional>

namespace Framework::Integrations::Shared::Messages {
    enum GameSyncMessages : uint32_t {
        GAME_SYNC_PLAYER_SPAWN = Networking::Messages::GameMessages::GAME_NEXT_MESSAGE_ID,
        GAME_SYNC_PLAYER_DESPAWN,
        GAME_SYNC_PLAYER_SET_POSITION,

        GAME_SYNC_VEHICLE_SPAWN,
        GAME_SYNC_VEHICLE_DESPAWN,
        GAME_SYNC_VEHICLE_SET_POSITION,

        GAME_SYNC_WEATHER_UPDATE,

        // Used by mods to define custom mod-specific messages, e.g. (MOD_SYNC_FOO = GAME_SYNC_USER_PACKET_ENUM + 1)
        GAME_SYNC_USER_PACKET_ENUM,
    };
} // namespace Framework::Integrations::Shared::Messages
