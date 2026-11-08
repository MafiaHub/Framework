/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "alloc_counter.h"

#include "networking/network_server.h"
#include "networking/replication/replication_connection.h"
#include "networking/replication/replication_manager.h"

#include <mafianet/get_time.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

// What interest management costs a server as it grows, measured the way a server runs it: one
// connection per player, each tick a RebuildInterest() and a ReplicaManager3 pass whose
// QueryReplicaList decides what each connection holds. "Sees" is what ReplicaManager3 has
// constructed on the connection, so these hold whatever the interest code does inside.
namespace InterestScaleTests {
    // ReplicaManager3 reads this clock; the tests step it so a tick takes as long as they say.
    inline std::atomic<MafiaNet::TimeUS> g_nowUS {1000000000};
    inline MafiaNet::TimeUS Now() {
        return g_nowUS.load();
    }

    using namespace Framework::Networking::Replication;

    struct World {
        static constexpr MafiaNet::Time kTickMs = 16;

        Framework::Networking::NetworkServer peer;
        ReplicationManager *manager = peer.GetReplicationManager();
        uint32_t type               = EntityRegistry::Get().Register<NetworkEntity>("Test::InterestScale");
        std::vector<NetworkEntity *> entities;
        std::vector<ReplicationConnection *> connections;

        World() {
            MafiaNet::SetTimeSourceOverride(&Now);
            manager->Init(&peer, true);
            manager->ConfigureGrid(100.0f, -100000.0f, 100000.0f);
            manager->SetAutoSerializeInterval(kTickMs);
        }

        ~World() {
            for (NetworkEntity *entity : entities) {
                manager->DestroyEntity(entity);
            }
            manager->Clear();
            MafiaNet::SetTimeSourceOverride(nullptr);
        }

        NetworkEntity *Spawn(float x, float z) {
            NetworkEntity *entity     = manager->CreateEntity(type);
            entity->position          = glm::vec3(x, 0.0f, z);
            entity->streaming.range   = 100.0f;
            entities.push_back(entity);
            return entity;
        }

        void Despawn(NetworkEntity *entity) {
            std::erase(entities, entity);
            manager->DestroyEntity(entity);
        }

        // A player: an avatar plus the connection that views through it.
        ReplicationConnection *Join(uint64_t guid, float x, float z) {
            NetworkEntity *avatar = Spawn(x, z);
            const MafiaNet::RakNetGUID rakGuid(guid);
            manager->SetViewer(MafiaNet::ToPeerGuid(rakGuid), avatar);
            auto *connection = static_cast<ReplicationConnection *>(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, rakGuid));
            // Validation needs the remote's reply; there is no remote here.
            connection->isValidated = true;
            manager->PushConnection(connection);
            connections.push_back(connection);
            return connection;
        }

        // One server tick that took `ms` of wall time.
        void Tick(MafiaNet::Time ms = kTickMs) {
            g_nowUS += ms * 1000;
            manager->RebuildInterest();
            manager->Update();
        }

        // A tick that really was slow, for interest code that reads the wall clock.
        void SlowTick(MafiaNet::Time ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            Tick(ms);
        }

        int CountSeeing(const std::vector<NetworkEntity *> &movers) const {
            int seeing = 0;
            for (size_t i = 0; i < connections.size(); ++i) {
                seeing += connections[i]->HasReplicaConstructed(movers[i]) ? 1 : 0;
            }
            return seeing;
        }
    };
} // namespace InterestScaleTests

MODULE(interest_scale, {
    using namespace InterestScaleTests;

    // Players 1 km apart, each with something 500 m away that it cannot see yet and that will move
    // in next to it without anything else happening. Whether a player sees its mover tells whether
    // its interest was recomputed.
    constexpr int kPlayers = 60;
    auto populate          = [](World &world, std::vector<NetworkEntity *> &movers) {
        for (int i = 0; i < kPlayers; ++i) {
            world.Join(100 + i, i * 1000.0f, 0.0f);
            movers.push_back(world.Spawn(i * 1000.0f, 500.0f));
        }
        // Everyone settles: every connection has its avatar and none has its mover.
        for (int i = 0; i < 4; ++i) {
            world.Tick();
        }
    };
    auto moveIn = [](std::vector<NetworkEntity *> &movers) {
        for (NetworkEntity *mover : movers) {
            mover->position.z = 10.0f;
        }
    };

    IT("bounds the viewers one slow tick recomputes, then reaches them all", {
        World world;
        world.manager->SetInterestRebuildInterval(100);
        std::vector<NetworkEntity *> movers;
        populate(world, movers);
        EQUALS(world.CountSeeing(movers), 0);

        moveIn(movers);
        // Several refresh intervals pass in one tick. Recomputing every viewer then is what turns a
        // slow tick into a slower next one; the work per tick has to stay what a normal tick does.
        world.SlowTick(250);
        const int afterOne        = world.CountSeeing(movers);
        const int perTickAtSpeed  = (kPlayers * World::kTickMs + 99) / 100;
        GREATER(afterOne, 0);
        LESSEREQ(afterOne, perTickAtSpeed);

        for (int i = 0; i < kPlayers && world.CountSeeing(movers) < kPlayers; ++i) {
            world.Tick();
        }
        EQUALS(world.CountSeeing(movers), kPlayers);
    });

    IT("streams a new entity in without recomputing every other viewer", {
        World world;
        world.manager->SetInterestRebuildInterval(1000000);
        std::vector<NetworkEntity *> movers;
        populate(world, movers);
        moveIn(movers);

        NetworkEntity *spawned = world.Spawn(30 * 1000.0f, 20.0f);
        world.Tick();
        EQUALS(world.connections[30]->HasReplicaConstructed(spawned), true);
        EQUALS(world.connections[29]->HasReplicaConstructed(spawned), false);
        // At most the one viewer whose routine turn this tick was; not all sixty.
        LESSEREQ(world.CountSeeing(movers), 2);
    });

    IT("streams a destroyed entity out without recomputing every other viewer", {
        World world;
        world.manager->SetInterestRebuildInterval(1000000);
        std::vector<NetworkEntity *> movers;
        populate(world, movers);
        NetworkEntity *doomed = world.Spawn(30 * 1000.0f, 20.0f);
        world.Tick();
        EQUALS(world.connections[30]->HasReplicaConstructed(doomed), true);
        moveIn(movers);

        world.Despawn(doomed);
        world.Tick();
        EQUALS(world.connections[30]->HasReplicaConstructed(doomed), false);
        LESSEREQ(world.CountSeeing(movers), 2);
    });

    IT("runs a tick in which nothing changed without allocating", {
        World world;
        world.manager->SetInterestRebuildInterval(100);
        // A crowd: every player sees every other one and a few hundred things around them.
        for (int i = 0; i < 32; ++i) {
            world.Join(100 + i, static_cast<float>(i), 0.0f);
        }
        for (int i = 0; i < 300; ++i) {
            world.Spawn(static_cast<float>(i % 50), static_cast<float>(i / 50));
        }
        for (int i = 0; i < 40; ++i) {
            world.Tick();
        }

        FrameworkTests::AllocationScope allocations;
        for (int i = 0; i < 20; ++i) {
            world.Tick();
        }
        EQUALS(allocations.Count(), static_cast<uint64_t>(0));
    });

    IT("recomputes a viewer's interest without allocating once it has settled", {
        World world;
        for (int i = 0; i < 300; ++i) {
            world.Spawn(static_cast<float>(i % 50), static_cast<float>(i / 50));
        }
        ReplicationConnection *viewer = world.Join(100, 0.0f, 0.0f);
        for (int i = 0; i < 4; ++i) {
            viewer->InvalidateInterest();
            world.Tick();
        }

        FrameworkTests::AllocationScope allocations;
        for (int i = 0; i < 20; ++i) {
            viewer->InvalidateInterest();
            world.Tick();
        }
        EQUALS(allocations.Count(), static_cast<uint64_t>(0));
    });
});
