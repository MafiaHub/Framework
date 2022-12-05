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
            uint64_t modelHash{};
            glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);
            std::string modelName;
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
            uint64_t owner   = 0;

            AssignOwnerProc assignOwnerProc;

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
            float range = 100.0f;
            uint64_t guid = (uint64_t)-1;
            std::string nickname;
            std::unordered_map<flecs::entity_t, StreamData> entities;
        };

        explicit Base(flecs::world &world) {
            world.module<Base>();

            // TODO expose STL types once https://github.com/SanderMertens/flecs/issues/712 is resolved.

            auto _transform = world.component<Transform>();
            auto _frame = world.component<Frame>();
            auto _streamable = world.component<Streamable>();
            auto _streamer = world.component<Streamer>();

            world.component<PendingRemoval>();
            world.component<ServerID>();

            // Windows bind metadata
            #ifdef _WIN32
            {
                auto _vec3 = world.component<glm::vec3>();
                auto _quat = world.component<glm::quat>();
                _vec3
                    .member<float>("x")
                    .member<float>("y")
                    .member<float>("z");
                _quat
                    .member<float>("w")
                    .member<float>("x")
                    .member<float>("y")
                    .member<float>("z");
                _transform
                    .member<glm::vec3>("pos")
                    .member<glm::quat>("rot")
                    .member<glm::vec3>("vel");
                _frame
                    .member<uint64_t>("modelHash")
                    .member<glm::vec3>("scale");
                _streamable
                    .member<int>("virtualWorld")
                    .member<bool>("isVisible")
                    .member<bool>("alwaysVisible")
                    .member<double>("updateInterval")
                    .member<uint64_t>("owner");
                _streamer
                    .member<float>("range")
                    .member<uint64_t>("guid");
            }
            #endif
        }

        static void SetupDefaultEvents(Streamable *streamable);
        static void SetupDefaultClientEvents(Streamable *streamable);
    };
} // namespace Framework::World::Modules
