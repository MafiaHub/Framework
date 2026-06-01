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

    // Configures a freshly created native entity for streaming. With native replication most of the
    // old per-entity streaming state is gone; this just stamps ownership.
    class StreamingFactory {
      public:
        void SetupServer(Replication::NetworkEntity *entity, uint64_t guid) {
            if (entity) {
                entity->ownerGUID = guid;
            }
        }
        void SetupClient(Replication::NetworkEntity *entity, uint64_t guid) {
            if (entity) {
                entity->ownerGUID = guid;
            }
        }
    };
} // namespace Framework::World::Archetypes
