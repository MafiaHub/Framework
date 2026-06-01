/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <flecs/distr/flecs.h>

// The module the scripting resource world imports. Networked entities live in
// networking/replication/ (Replication::NetworkEntity), so this registers no components.
namespace Framework::World::Modules {
    struct Base {
        explicit Base(flecs::world &world) {
            world.module<Base>();
        }
    };
} // namespace Framework::World::Modules
