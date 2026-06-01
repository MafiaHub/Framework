/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/replication/network_entity.h"

#include <cstdint>

namespace Framework::World::Archetypes {
    namespace Replication = Framework::Networking::Replication;

    // Configures a native entity as a player's avatar: it owns itself and (on the server) acts as the
    // connection's viewer — its position/streamRange drive that client's interest set. The caller
    // must still register it with the ReplicationManager as the viewer for the player's GUID
    // (ReplicationManager::SetViewer). Player metadata such as nickname or hardware id is no longer
    // tracked here; carry it on your game's player NetworkEntity subclass.
    class PlayerFactory {
      public:
        void SetupClient(Replication::NetworkEntity *entity, uint64_t guid) {
            if (entity) {
                entity->ownerGUID = guid;
            }
        }

        void SetupServer(Replication::NetworkEntity *entity, uint64_t guid) {
            if (entity) {
                entity->ownerGUID = guid;
                entity->isViewer  = true;
            }
        }
    };
} // namespace Framework::World::Archetypes
