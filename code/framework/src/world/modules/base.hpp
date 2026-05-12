/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <flecs/distr/flecs.h>
#include <function2.hpp>
#include <glm/ext.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Framework::Networking {
    class NetworkPeer;
} // namespace Framework::Networking

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

        struct TickRateRegulator {
            Transform snapshot {};
            uint16_t lastGenID = 0;
        };

        struct Frame {
            uint64_t modelHash {};
            glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);
            std::string modelName;
        };

        // Zero-size tag: presence on an entity schedules it for removal.
        struct PendingRemoval {};

        // Zero-size tag: entity is destroyed when the owning resource is reloaded.
        struct RemovedOnResourceReload {};

        // Zero-size tag relation: (DependsOn, target) means the source entity's
        // streaming visibility depends on `target`. If any target is visible the
        // source is forced to be visible as well.
        struct DependsOn {};

        struct ServerID {
            flecs::entity_t id;
        };

        // Singleton component holding the active NetworkPeer. Systems and
        // observers read it via world.get<NetworkPeerHandle>() instead of
        // capturing a raw pointer in lambdas.
        struct NetworkPeerHandle {
            Framework::Networking::NetworkPeer *peer = nullptr;
        };

        // Custom events emitted by the server's streaming loop. Framework
        // observers translate them into network messages; mods can subscribe
        // alongside for custom logic. The event struct itself carries the
        // payload (peer + target guid), accessed inside the observer via
        // flecs::iter::param().
        struct StreamEventBase {
            Framework::Networking::NetworkPeer *peer = nullptr;
            uint64_t targetGuid                      = 0;
        };
        struct StreamSpawnEvent       : StreamEventBase {};
        struct StreamDespawnEvent     : StreamEventBase {};
        struct StreamUpdateEvent      : StreamEventBase {};
        struct StreamOwnerUpdateEvent : StreamEventBase {};
        struct StreamSelfUpdateEvent  : StreamEventBase {};

        struct Streamable {
            using IsVisibleProc         = fu2::function<bool(flecs::entity lhs, flecs::entity rhs) const>;
            using AssignOwnerProc       = fu2::function<bool(flecs::entity e, Streamable &streamable)>;
            using OnDisconnectProc      = fu2::function<void(flecs::entity e)>;
            using OnUpdateTransformProc = fu2::function<void(flecs::entity e)>;

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

            // If set to true, the owner will not be assigned automatically by the framework
            bool assignOwnerManually = false;

            // Allows custom owner assignment logic, if method returns true we bypass framework's proximity based owner assignment
            AssignOwnerProc assignOwnerProc;

            // Per-entity callbacks invoked by the framework after each network
            // event fires. Mods set these on individual entities to layer
            // custom behavior on top of the framework's spawn/despawn/update
            // messages without registering global observers.
            struct Events {
                using Proc = fu2::function<bool(Framework::Networking::NetworkPeer *, uint64_t, flecs::entity) const>;
                Proc spawnProc;
                Proc despawnProc;
                Proc selfUpdateProc;
                Proc updateProc;
                Proc ownerUpdateProc;

                // Events used locally for special needs
                // These are NOT emitted through the network!
                OnDisconnectProc disconnectProc; // called when the client disconnects from server
                OnUpdateTransformProc updateTransformProc; // called whenever the server enforces a new transform upon the entity
            };

            Events modEvents;

            // Custom visibility proc that either complements the existing heuristic or replaces it
            HeuristicMode isVisibleHeuristic = HeuristicMode::ADD;
            IsVisibleProc isVisibleProc;

            // Controls whether this entity gets to be updated continuously or not
            // When set to false, we only stream spawn and despawn events, useful for immovable objects
            bool performTickUpdates = true;

            Events& GetModEvents() {
                return modEvents;
            }
        };

        struct Streamer {
            using CollectRangeExemptEntities = fu2::function<void(flecs::entity e, Streamer &streamer)>;
            struct StreamData {
                double lastUpdate = 0.0;
            };
            float range          = 100.0f;
            uint64_t guid        = 0xFFFFFFFFFFFFFFFF;
            uint16_t playerIndex = 0xFFFF;
            std::string nickname;
            std::string hardwareId;
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

            // Empty structs are auto-registered as zero-size tags by Flecs.
            world.component<RemovedOnResourceReload>();
            world.component<PendingRemoval>();

            // DependsOn is acyclic by contract — visibility recursion would
            // diverge otherwise. We also tell Flecs to drop the relation pair
            // automatically when the target dies, so dependents stop carrying
            // dangling references the moment a dependency entity is destroyed.
            world.component<DependsOn>()
                .add(flecs::Acyclic)
                .add(flecs::OnDeleteTarget, flecs::Remove);
            world.component<ServerID>();
            world.component<TickRateRegulator>();
            world.component<NetworkPeerHandle>().add(flecs::Singleton);

            // Streaming event types — registered as components so they can be
            // used as flecs event ids with payload.
            world.component<StreamSpawnEvent>();
            world.component<StreamDespawnEvent>();
            world.component<StreamUpdateEvent>();
            world.component<StreamOwnerUpdateEvent>();
            world.component<StreamSelfUpdateEvent>();

            // Component metadata for the Flecs explorer / REST API. Used to
            // be Windows-only, but the explorer runs on every platform and
            // there is no runtime cost to keeping these registrations on.
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
        }

        // Framework streaming-event observers translating the custom events
        // into network messages. Registered once at engine init time.
        static void RegisterServerStreamObservers(flecs::world &world);
        static void RegisterClientStreamObservers(flecs::world &world);

        static void SetupServerReceivers(Framework::Networking::NetworkPeer *net, Framework::World::Engine *worldEngine);
        static void SetupClientReceivers(Framework::Networking::NetworkPeer *net, Framework::World::ClientEngine *worldEngine, Framework::World::Archetypes::StreamingFactory *streamingFactory);
    };
} // namespace Framework::World::Modules
