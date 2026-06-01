/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <flecs/distr/flecs.h>

// NOTE: The flecs-based streaming model (Transform/Frame/Streamable/Streamer components, the
// per-tick StreamEntities/AssignEntityOwnership/TickRateRegulator systems and the GameSync*
// messages) has been removed in favour of native MafiaNet replication. Networked entities are now
// Replication::NetworkEntity objects owned by Replication::ReplicationManager; see
// networking/replication/. This Base module is retained only so the scripting resource world has a
// module to import, and currently registers no networked components.
namespace Framework::World::Modules {
    struct Base {
        explicit Base(flecs::world &world) {
            world.module<Base>();
        }
    };
} // namespace Framework::World::Modules
