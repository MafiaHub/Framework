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

        struct PendingRemoval {};

        struct RemovedOnResourceReload {};

        // Tag: Entity is hidden from streaming (inverse of isVisible)
        struct Hidden {};

        // Tag: Entity is always visible regardless of distance
        struct AlwaysVisible {};

        // Tag: Entity does not receive tick updates (inverse of performTickUpdates)
        struct NoTickUpdates {};

        // Tag: Entity ownership is assigned manually, not by proximity
        struct ManualOwnership {};

        // Tag: Custom visibility proc completely replaces framework heuristics
        struct VisibilityReplace {};

        // Tag: Custom visibility proc replaces only position/distance check
        struct VisibilityReplacePosition {};

        // Relation: Entity is owned by a streamer entity
        // Usage: entity.add<OwnedBy>(streamerEntity)
        struct OwnedBy {};

        // Relation: Entity is currently being streamed to a streamer
        // Usage: entity.add<StreamedTo>(streamerEntity)
        // OnAdd triggers spawn RPC, OnRemove triggers despawn RPC
        struct StreamedTo {
            double lastUpdate = 0.0;
        };

        // Relation: Entity exists in a virtual world
        // Usage: entity.add<InVirtualWorld>(worldEntity)
        // Use Engine::GetOrCreateVirtualWorld(id) to get world entities
        struct InVirtualWorld {};

        // Tag: Marks an entity as a virtual world instance
        struct VirtualWorld {
            int id = 0;
        };

        struct ServerID {
            flecs::entity_t id;
        };

        struct Streamable {
            using IsVisibleProc         = fu2::function<bool(flecs::entity lhs, flecs::entity rhs) const>;
            using AssignOwnerProc       = fu2::function<bool(flecs::entity e, Streamable &streamable)>;
            using OnDisconnectProc      = fu2::function<void(flecs::entity e)>;
            using OnUpdateTransformProc = fu2::function<void(flecs::entity e)>;

            int virtualWorld             = 0;

            double defaultUpdateInterval = (1000.0 / 60.0); // 16.1667~ ms interval
            double updateInterval        = defaultUpdateInterval;

            // Owner GUID for network synchronization (derived from OwnedBy relation on server)
            uint64_t owner               = 0;

            // Allows custom owner assignment logic, if method returns true we bypass framework's proximity based owner assignment
            AssignOwnerProc assignOwnerProc;

            // Custom visibility proc for additional visibility checks
            // Use VisibilityReplace tag to completely replace framework heuristics
            // Use VisibilityReplacePosition tag to replace only distance check
            // Without either tag, proc is combined with framework checks (AND)
            IsVisibleProc isVisibleProc;

            // Used to specify list of entities this streamable entity relies on.
            // If any of these entities are visible and ours is not, we force ours to be visible too.
            std::vector<flecs::entity> dependentEntities;

            // Local lifecycle callbacks (not network events)
            OnDisconnectProc disconnectProc;       // called when the client disconnects from server
            OnUpdateTransformProc updateTransformProc; // called whenever the server enforces a new transform upon the entity
        };

        struct Streamer {
            using CollectRangeExemptEntities = fu2::function<void(flecs::entity e, Streamer &streamer)>;
            float range          = 100.0f;
            uint64_t guid        = 0xFFFFFFFFFFFFFFFF;
            uint16_t playerIndex = 0xFFFF;
            std::string nickname;
            std::string hardwareId;
            std::unordered_set<flecs::entity_t> rangeExemptEntities;
            CollectRangeExemptEntities collectRangeExemptEntitiesProc;
        };

        // Prefab entities for efficient archetype instantiation
        static inline flecs::entity StreamableEntityPrefab;
        static inline flecs::entity StreamerEntityPrefab;

        explicit Base(flecs::world &world) {
            world.module<Base>();

            // TODO expose STL types once https://github.com/SanderMertens/flecs/issues/712 is resolved.

            auto _transform  = world.component<Transform>();
            auto _frame      = world.component<Frame>();
            auto _streamable = world.component<Streamable>();
            auto _streamer   = world.component<Streamer>();

            world.component<RemovedOnResourceReload>();
            world.component<PendingRemoval>();
            world.component<ServerID>();
            world.component<TickRateRegulator>();

            // Visibility and behavior tags
            world.component<Hidden>();
            world.component<AlwaysVisible>();
            world.component<NoTickUpdates>();
            world.component<ManualOwnership>();
            world.component<VisibilityReplace>();
            world.component<VisibilityReplacePosition>();
            world.component<OwnedBy>();
            world.component<StreamedTo>();
            world.component<InVirtualWorld>();
            world.component<VirtualWorld>();

            // Entity prefabs for efficient instantiation (LP-3)
            // StreamableEntity: base components for any streamable entity
            StreamableEntityPrefab = world.prefab("StreamableEntityPrefab")
                .add<Transform>()
                .add<Streamable>()
                .add<TickRateRegulator>();

            // StreamerEntity: components for player/streamer entities
            StreamerEntityPrefab = world.prefab("StreamerEntityPrefab")
                .is_a(StreamableEntityPrefab)
                .add<Streamer>();

// Windows bind metadata
#ifdef _WIN32
            {
                auto _vec3 = world.component<glm::vec3>();
                auto _quat = world.component<glm::quat>();
                _vec3.member<float>("x").member<float>("y").member<float>("z");
                _quat.member<float>("w").member<float>("x").member<float>("y").member<float>("z");
                _transform.member<glm::vec3>("pos").member<glm::quat>("rot").member<glm::vec3>("vel");
                _frame.member<uint64_t>("modelHash").member<glm::vec3>("scale");
                _streamable.member<int>("virtualWorld").member<double>("updateInterval").member<uint64_t>("owner");
                _streamer.member<float>("range").member<uint64_t>("guid");
            }
#endif
        }

        static void SetupServerReceivers(Framework::Networking::NetworkPeer *net, Framework::World::Engine *worldEngine);
        static void SetupClientReceivers(Framework::Networking::NetworkPeer *net, Framework::World::ClientEngine *worldEngine, Framework::World::Archetypes::StreamingFactory *streamingFactory);
    };
} // namespace Framework::World::Modules
