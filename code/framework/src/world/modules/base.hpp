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

#define CALL_CUSTOM_PROC(kind)                                                                                                                                                     \
    const auto streamable = e.get<Framework::World::Modules::Base::Streamable>();                                                                                                  \
    if (streamable != nullptr) {                                                                                                                                                   \
        if (streamable->modEvents.##kind != nullptr) {                                                                                                                             \
            streamable->modEvents.##kind(guid, e);                                                                                                                                 \
        }                                                                                                                                                                          \
    }

#define ENTITY_SETUP_DEFAULT_EVENTS(streamable, peer)                                                                                                                              \
    streamable->events.spawnProc = [&](uint64_t guid, flecs::entity &e) {                                                                                                          \
        Framework::Networking::Messages::GameSyncEntitySpawn entitySpawn;                                                                                                          \
        const auto tr = e.get<Framework::World::Modules::Base::Transform>();                                                                                                       \
        if (tr)                                                                                                                                                                    \
            entitySpawn.FromParameters(*tr);                                                                                                                                       \
        peer->Send(entitySpawn, guid);                                                                                                                                             \
        CALL_CUSTOM_PROC(spawnProc);                                                                                                                                               \
        return true;                                                                                                                                                               \
    };                                                                                                                                                                             \
                                                                                                                                                                                   \
    streamable->events.despawnProc = [&](uint64_t guid, flecs::entity &e) {                                                                                                        \
        Framework::Networking::Messages::GameSyncEntityDespawn entityDespawn;                                                                                                      \
        peer->Send(entityDespawn, guid);                                                                                                                                           \
        CALL_CUSTOM_PROC(despawnProc);                                                                                                                                             \
        return true;                                                                                                                                                               \
    };                                                                                                                                                                             \
                                                                                                                                                                                   \
    streamable->events.selfUpdateProc = [&](uint64_t guid, flecs::entity &e) {                                                                                                     \
        Framework::Networking::Messages::GameSyncEntitySelfUpdate entitySelfUpdate;                                                                                                \
        peer->Send(entitySelfUpdate, guid);                                                                                                                                        \
        CALL_CUSTOM_PROC(selfUpdateProc);                                                                                                                                          \
        return true;                                                                                                                                                               \
    };                                                                                                                                                                             \
                                                                                                                                                                                   \
    streamable->events.updateProc = [&](uint64_t guid, flecs::entity &e) {                                                                                                         \
        Framework::Networking::Messages::GameSyncEntityUpdate entityUpdate;                                                                                                        \
        const auto tr = e.get<Framework::World::Modules::Base::Transform>();                                                                                                       \
        if (tr)                                                                                                                                                                    \
            entityUpdate.FromParameters(*tr);                                                                                                                                      \
        peer->Send(entityUpdate, guid);                                                                                                                                            \
        CALL_CUSTOM_PROC(updateProc);                                                                                                                                              \
        return true;                                                                                                                                                               \
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

        struct PendingRemoval {};

        struct Streamable {
            int virtualWorld        = 0;
            bool isVisible          = true;
            bool alwaysVisible      = false;
            int64_t updateFrequency = 1667;
            flecs::entity_t owner   = 0;

            struct Events {
                using Proc = std::function<bool(uint64_t, flecs::entity &)>;
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
    };
} // namespace Framework::World::Modules
