/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <flecs/flecs.h>
#include <functional>
#include <glm/ext.hpp>
#include <string>
#include <unordered_map>

namespace Framework::Networking {
    class NetworkPeer;
};

namespace Framework::World::Modules {
    struct Base {
        struct Transform {
            glm::vec3 pos;
            glm::vec3 vel;
            glm::quat rot = glm::identity<glm::quat>();
        };

        struct Frame {
            flecs::string modelName;
            glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);
        };

        struct PendingRemoval {
            uint8_t _unused;
        };

        struct Streamable {
            int virtualWorld        = 0;
            bool isVisible          = true;
            bool alwaysVisible      = false;
            int64_t updateFrequency = 1667;
            flecs::entity_t owner   = 0;

            struct Events {
                using Proc = std::function<bool(Framework::Networking::NetworkPeer *, uint64_t, flecs::entity &)>;
                Proc spawnProc;
                Proc despawnProc;
                Proc selfUpdateProc;
                Proc updateProc;
            };

            Events events;

            // Extra set of events so mod can supply custom data.
            Events modEvents;
        };

        struct Streamer {
            struct StreamData {
                int64_t lastUpdate = 0;
            };
            std::unordered_map<flecs::entity_t, StreamData> entities;
            float range = 100.0f;

            uint64_t guid = (uint64_t)-1;
        };

        Base(flecs::world &world) {
            world.module<Base>();

            world.component<Transform>();
            world.component<Frame>();
            world.component<Streamable>();
            world.component<Streamer>();
            world.component<PendingRemoval>();
        }

        static void SetupDefaultEvents(Streamable *streamable);
    };
} // namespace Framework::World::Modules
