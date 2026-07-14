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

    // Configures an entity as a player's avatar: it owns itself and (on the server) acts as the
    // connection's viewer — its position/streamRange drive that client's interest set. The caller
    // registers it as the viewer for the player's GUID via ReplicationManager::SetViewer. Player
    // metadata (nickname, hardware id) belongs on the game's player NetworkEntity subclass.
    class PlayerArchetype final {
      public:
        void SetupClient(Replication::NetworkEntity *entity, MafiaNet::PeerGuid guid) {
            if (entity) {
                entity->ownerGUID = guid;
            }
        }

        void SetupServer(Replication::NetworkEntity *entity, MafiaNet::PeerGuid guid) {
            if (entity) {
                entity->ownerGUID          = guid;
                entity->streaming.isViewer = true;
            }
        }
    };
} // namespace Framework::World::Archetypes
