/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <flecs/flecs.h>
#include <string>

namespace Framework::Integrations::Shared::Modules {
    struct Mod {
        Mod(flecs::world &world) {
            world.module<Mod>();
        }
    };
} // namespace Framework::Integrations::Shared::Modules
