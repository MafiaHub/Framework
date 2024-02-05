/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
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
#include <unordered_set>

namespace Framework::Networking {
    class NetworkPeer;
}; // namespace Framework::Networking

namespace Framework::World {
    class Engine;
    class ClientEngine;

    namespace Archetypes {
        class StreamingFactory;
    }
} // namespace Framework::World

namespace Framework::World::Modules {
    struct Base {
        struct Transform {
          private:
            uint16_t genID = 0;

          public:
            glm::vec3 pos {};
            glm::vec3 vel {};
            glm::quat rot = glm::identity<glm::quat>();

            uint16_t GetGeneration() const {
                return genID;
            }

            void IncrementGeneration() {
                ++genID;
            }

            bool ValidateGeneration(const Transform &tr) const {
                return genID == tr.genID;
            }
        };

        struct TickRateRegulator: public Transform {
            uint16_t lastGenID = 0;
        };

        struct Frame {
            uint64_t modelHash {};
            glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);
            std::string modelName;
        };

        struct PendingRemoval {
            [[maybe_unused]] uint8_t _unused;
        };

        struct RemovedOnGameModeReload {
            [[maybe_unused]] uint8_t _unused;
        };

        struct ServerID {
            flecs::entity_t id;
        };

        struct Streamable {
            using IsVisibleProc   = fu2::function<bool(flecs::entity lhs, flecs::entity rhs) const>;
            using AssignOwnerProc = fu2::function<bool(flecs::entity e, Streamable &streamable)>;
            enum class HeuristicMode {
                ADD,
                REPLACE,
                REPLACE_POSITION
            };

            int virtualWorld             = 0;
            bool isVisible               = true;
            bool alwaysVisible           = false;
            double defaultUpdateInterval = (1000.0 / 60.0); // 16.1667~ ms interval
            double updateInterval        = defaultUpdateInterval;
            uint64_t owner               = 0;

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
            using CollectRangeExemptEntities = fu2::function<void(flecs::entity e, Streamer &streamer)>;
            struct StreamData {
                double lastUpdate = 0.0;
            };
            float range          = 100.0f;
            uint64_t guid        = (uint64_t)-1;
            uint16_t playerIndex = (uint64_t)-1;
            std::string nickname;
            std::unordered_map<flecs::entity_t, StreamData> entities;
            std::unordered_set<flecs::entity_t> rangeExemptEntities;
            CollectRangeExemptEntities collectRangeExemptEntitiesProc;
        };

        explicit Base(flecs::world &world) {
            world.module<Base>();

            // TODO expose STL types once https://github.com/SanderMertens/flecs/issues/712 is resolved.

            auto _transform  = world.component<Transform>();
            auto _frame      = world.component<Frame>();
            auto _streamable = world.component<Streamable>();
            auto _streamer   = world.component<Streamer>();

            world.component<RemovedOnGameModeReload>();
            world.component<PendingRemoval>();
            world.component<ServerID>();
            world.component<TickRateRegulator>();

// Windows bind metadata
#ifdef _WIN32
            {
                auto _vec3 = world.component<glm::vec3>();
                auto _quat = world.component<glm::quat>();
                _vec3.member<float>("x").member<float>("y").member<float>("z");
                _quat.member<float>("w").member<float>("x").member<float>("y").member<float>("z");
                _transform.member<glm::vec3>("pos").member<glm::quat>("rot").member<glm::vec3>("vel");
                _frame.member<uint64_t>("modelHash").member<glm::vec3>("scale");
                _streamable.member<int>("virtualWorld").member<bool>("isVisible").member<bool>("alwaysVisible").member<double>("updateInterval").member<uint64_t>("owner");
                _streamer.member<float>("range").member<uint64_t>("guid");
            }
#endif
        }

        static void SetupServerEmitters(Streamable *streamable);
        static void SetupClientEmitters(Streamable *streamable);
        static void SetupServerReceivers(Framework::Networking::NetworkPeer *net, Framework::World::Engine *worldEngine);
        static void SetupClientReceivers(Framework::Networking::NetworkPeer *net, Framework::World::ClientEngine *worldEngine, Framework::World::Archetypes::StreamingFactory *streamingFactory);
    };
} // namespace Framework::World::Modules
