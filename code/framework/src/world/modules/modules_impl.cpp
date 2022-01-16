/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "base.hpp"
#include "networking/messages/game_sync/entity_messages.h"
#include "networking/network_peer.h"

#define CALL_CUSTOM_PROC(kind)                                                                                                                                                     \
    const auto streamable = e.get<Framework::World::Modules::Base::Streamable>();                                                                                                  \
    if (streamable != nullptr) {                                                                                                                                                   \
        if (streamable->modEvents.##kind != nullptr) {                                                                                                                             \
            streamable->modEvents.##kind(peer, guid, e);                                                                                                                           \
        }                                                                                                                                                                          \
    }

namespace Framework::World::Modules {
    void Base::SetupDefaultEvents(Streamable *streamable) {
        streamable->events.spawnProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntitySpawn entitySpawn;
            const auto tr = e.get<Framework::World::Modules::Base::Transform>();
            if (tr)
                entitySpawn.FromParameters(*tr);
            peer->Send(entitySpawn, guid);
            CALL_CUSTOM_PROC(spawnProc);
            return true;
        };

        streamable->events.despawnProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntityDespawn entityDespawn;
            peer->Send(entityDespawn, guid);
            CALL_CUSTOM_PROC(despawnProc);
            return true;
        };

        streamable->events.selfUpdateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntitySelfUpdate entitySelfUpdate;
            peer->Send(entitySelfUpdate, guid);
            CALL_CUSTOM_PROC(selfUpdateProc);
            return true;
        };

        streamable->events.updateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Framework::Networking::Messages::GameSyncEntityUpdate entityUpdate;
            const auto tr = e.get<Framework::World::Modules::Base::Transform>();
            const auto es = e.get<Framework::World::Modules::Base::Streamable>();
            if (tr && es)
                entityUpdate.FromParameters(*tr, es->owner);
            peer->Send(entityUpdate, guid);
            CALL_CUSTOM_PROC(updateProc);
            return true;
        };
    }
} // namespace Framework::World::Modules
