/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <flecs/flecs.h>
#include <function2.hpp>
#include <glm/ext.hpp>
#include <string>
#include <unordered_map>

namespace Framework::Networking {
    class NetworkPeer;
};

namespace Framework::World::Modules {
    struct Base {
        struct Transform {
            glm::vec3 pos{};
            glm::vec3 vel{};
            glm::quat rot = glm::identity<glm::quat>();
        };

        struct Frame {
            std::string modelName;
            uint64_t modelHash{};
            glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);
        };

        struct PendingRemoval {
            [[maybe_unused]] uint8_t _unused;
        };

        struct ServerID {
            flecs::entity_t id;
        };

        struct Streamable {
            using IsVisibleProc = fu2::function<bool(flecs::entity lhs, flecs::entity rhs) const>;
            using AssignOwnerProc = fu2::function<bool(flecs::entity e, Streamable &streamable)>;
            enum class HeuristicMode { ADD, REPLACE, REPLACE_POSITION };

            int virtualWorld        = 0;
            bool isVisible          = true;
            bool alwaysVisible      = false;
            double updateInterval = (1000.0/60.0); // 16.1667~ ms interval

            AssignOwnerProc assignOwnerProc;
            uint64_t owner   = 0;

            struct Events {
                using Proc = fu2::function<bool(Framework::Networking::NetworkPeer *, uint64_t, flecs::entity) const>;
                Proc spawnProc;
                Proc despawnProc;
                Proc selfUpdateProc;
                Proc updateProc;
                Proc ownerUpdateProc;
            };

            // Extra set of events so mod can supply custom data.
            Events modEvents;

            // Custom visibility proc that either complements the existing heuristic or replaces it
            HeuristicMode isVisibleHeuristic = HeuristicMode::ADD;
            IsVisibleProc isVisibleProc;

            // Framework-level events.
            friend Base;
            private:
            Events events;

            public:
            Events GetBaseEvents() const {
                return events;
            }

            [[maybe_unused]] Events GetModEvents() const {
                return modEvents;
            }
        };

        struct Streamer {
            struct StreamData {
                double lastUpdate = 0.0;
            };
            std::unordered_map<flecs::entity_t, StreamData> entities;
            float range = 100.0f;

            uint64_t guid = (uint64_t)-1;
            std::string nickname;
        };

        explicit Base(flecs::world &world) {
            world.module<Base>();

            world.component<Transform>();
            world.component<Frame>();
            world.component<Streamable>();
            world.component<Streamer>();
            world.component<PendingRemoval>();
            world.component<ServerID>();
        }

        static void SetupDefaultEvents(Streamable *streamable);
        static void SetupDefaultClientEvents(Streamable *streamable);
    };
} // namespace Framework::World::Modules
