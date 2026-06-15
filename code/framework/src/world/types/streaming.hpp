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

    // Stamps ownership on a streamed entity.
    class StreamingArchetype {
      public:
        void SetupServer(Replication::NetworkEntity *entity, MafiaNet::PeerGuid guid) {
            if (entity) {
                entity->ownerGUID = guid;
            }
        }
        void SetupClient(Replication::NetworkEntity *entity, MafiaNet::PeerGuid guid) {
            if (entity) {
                entity->ownerGUID = guid;
            }
        }
    };
} // namespace Framework::World::Archetypes
