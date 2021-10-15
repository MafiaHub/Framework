/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "world/modules/base.hpp"
#include "logging/logger.h"
#include "utils/time.h"

#include <flecs/addons/timer.h>
#include <flecs/flecs.h>
#include <functional>
#include <glm/ext.hpp>
#include <slikenet/types.h>
#include <unordered_map>

namespace Framework::Integrations::Shared::Modules {
    struct Network {
        struct Streamable {
            int virtualWorld        = 0;
            bool isVisible          = true;
            bool alwaysVisible      = false;
            int64_t updateFrequency = 1667;
            flecs::entity_t owner   = 0;

            struct Events {
                using Proc = std::function<bool(SLNet::RakNetGUID, flecs::entity &)>;
                Proc spawnProc;
                Proc despawnProc;
                Proc selfUpdateProc;
                Proc updateProc;
            } events;
        };

        struct Streamer {
            struct StreamData {
                int64_t lastUpdate = 0;
            };
            std::unordered_map<flecs::entity_t, StreamData> entities;
            float range = 100.0f;

            SLNet::RakNetGUID guid = SLNet::UNASSIGNED_RAKNET_GUID;
        };

        Network(flecs::world &world) {
            world.module<Network>();

            world.component<Streamable>();
            world.component<Streamer>();
        }
    };
} // namespace Framework::Integrations::Shared::Modules
