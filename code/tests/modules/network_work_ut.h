/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_server.h"
#include "networking/replication/replication_connection.h"
#include "networking/replication/replication_manager.h"

#include <vector>

MODULE(network_work, {
    // Players each with a mover that comes into range without anything else happening: a player
    // that sees its mover has had its interest recomputed since.
    struct Turns {
        Framework::Networking::NetworkServer peer;
        Framework::Networking::Replication::ReplicationManager *manager = peer.GetReplicationManager();
        std::vector<Framework::Networking::Replication::NetworkEntity *> entities;
        std::vector<Framework::Networking::Replication::NetworkEntity *> movers;
        std::vector<Framework::Networking::Replication::ReplicationConnection *> connections;

        Turns(int players, uint32_t intervalMs, MafiaNet::Time tickMs) {
            using namespace Framework::Networking::Replication;
            manager->Init(&peer, true);
            manager->ConfigureGrid(100, -100000, 100000);
            manager->SetInterestRebuildInterval(intervalMs);
            manager->SetAutoSerializeInterval(tickMs);
            const auto type = EntityRegistry::Get().Register<NetworkEntity>("Test::InterestTurns");
            for (int i = 0; i < players; ++i) {
                NetworkEntity *avatar = manager->CreateEntity(type);
                NetworkEntity *mover  = manager->CreateEntity(type);
                avatar->position.x    = i * 1000.0f;
                mover->position       = glm::vec3(i * 1000.0f, 0.0f, 500.0f);
                entities.push_back(avatar);
                entities.push_back(mover);
                movers.push_back(mover);
                const MafiaNet::RakNetGUID guid(1000 + i);
                manager->SetViewer(MafiaNet::ToPeerGuid(guid), avatar);
                manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, guid));
                connections.push_back(static_cast<ReplicationConnection *>(manager->GetConnectionByGUID(guid)));
            }
        }
        ~Turns() {
            for (auto *entity : entities) {
                manager->DestroyEntity(entity);
            }
            manager->Clear();
        }

        void Tick() {
            manager->RebuildInterest();
            for (auto *connection : connections) {
                DataStructures::List<MafiaNet::Replica3 *> create, destroy;
                connection->QueryReplicaList(create, destroy);
            }
        }
        int Seeing() const {
            int seeing = 0;
            for (size_t i = 0; i < connections.size(); ++i) {
                seeing += connections[i]->IsRelevant(movers[i]) ? 1 : 0;
            }
            return seeing;
        }
    };

    IT("recomputes the tick rate's share of viewers each tick, round robin", {
        // 20 viewers every 100 ms at 10 ms ticks: two a tick, each once in ten ticks.
        Turns turns(20, 100, 10);
        turns.Tick(); // a viewer's first set is computed at once
        EQUALS(turns.Seeing(), 0);
        for (auto *mover : turns.movers) {
            mover->position.z = 10.0f;
        }
        for (int tick = 1; tick <= 10; ++tick) {
            turns.Tick();
            EQUALS(turns.Seeing(), tick * 2);
        }
    });

    IT("recomputes every viewer every tick when the interval is zero", {
        Turns turns(5, 0, 10);
        turns.Tick();
        for (auto *mover : turns.movers) {
            mover->position.z = 10.0f;
        }
        turns.Tick();
        EQUALS(turns.Seeing(), 5);
    });

    IT("streams an entity a viewer is shown in at its next query, without recomputing it", {
        Turns turns(3, 1000000, 10);
        turns.Tick();
        for (auto *mover : turns.movers) {
            mover->position.z = 10.0f;
        }
        auto *spawned = turns.manager->CreateEntity(turns.entities.front()->GetTypeId());
        spawned->position.x = 1000.0f + 20.0f;
        turns.entities.push_back(spawned);
        turns.Tick();
        EQUALS(turns.connections[1]->IsRelevant(spawned), true);
        EQUALS(turns.connections[0]->IsRelevant(spawned), false);
        EQUALS(turns.Seeing(), 0);
    });

    IT("refreshes a viewer at once for deletions, world changes and its own ownership changes only", {
        using namespace Framework::Networking::Replication;
        Framework::Networking::NetworkServer peer;
        auto *manager = peer.GetReplicationManager();
        manager->Init(&peer, true);
        struct Cleanup {
            ReplicationManager *manager;
            ~Cleanup() {
                manager->Clear();
            }
        } cleanup {manager};
        manager->ConfigureGrid(100, -4096, 4096);
        // Routine refreshes are far in the future: every change below has to bypass them.
        constexpr uint32_t kFarInterval = 1000000;
        manager->SetInterestRebuildInterval(kFarInterval);
        const auto type            = EntityRegistry::Get().Register<NetworkEntity>("Test::InterestRefresh");
        auto *viewer               = manager->CreateEntity(type);
        auto *bystander            = manager->CreateEntity(type);
        auto *nearby               = manager->CreateEntity(type);
        auto *distant              = manager->CreateEntity(type);
        viewer->streaming.range    = 100;
        bystander->streaming.range = 100;
        distant->position.x        = 1000;
        const MafiaNet::RakNetGUID viewerGuid(77), bystanderGuid(78);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer);
        manager->SetViewer(MafiaNet::ToPeerGuid(bystanderGuid), bystander);
        manager->RebuildInterest();
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, bystanderGuid));
        auto *viewerConnection    = static_cast<ReplicationConnection *>(manager->GetConnectionByGUID(viewerGuid));
        auto *bystanderConnection = static_cast<ReplicationConnection *>(manager->GetConnectionByGUID(bystanderGuid));

        // Whether the connection's interest set holds the entity once it has been queried.
        auto sees = [](ReplicationConnection *connection, NetworkEntity *entity) {
            DataStructures::List<MafiaNet::Replica3 *> create, destroy;
            connection->QueryReplicaList(create, destroy);
            return connection->IsRelevant(entity);
        };
        // Moves the index on without any refresh-forcing change: cached sets keep the old picture.
        auto rebuildQuietly = [&] {
            manager->RebuildInterest();
        };

        EQUALS(sees(viewerConnection, nearby), true);
        EQUALS(sees(viewerConnection, distant), false);
        EQUALS(sees(bystanderConnection, nearby), true);

        nearby->position.x = 2000;
        rebuildQuietly();
        EQUALS(sees(viewerConnection, nearby), true);
        EQUALS(sees(bystanderConnection, nearby), true);

        // The new owner refreshes at once; a viewer the handover does not concern keeps its phase.
        manager->SetOwner(distant, MafiaNet::ToPeerGuid(viewerGuid));
        EQUALS(sees(viewerConnection, distant), true);
        EQUALS(sees(viewerConnection, nearby), false);
        EQUALS(sees(bystanderConnection, nearby), true);

        // A destruction reaches every viewer before its phase.
        manager->DestroyEntity(nearby);
        EQUALS(sees(bystanderConnection, nearby), false);

        // Owned entities bypass world culling, so hand it back before testing a dimension change.
        distant->position.x = 10;
        manager->SetOwner(distant, MafiaNet::UNASSIGNED_PEER_GUID);
        rebuildQuietly();
        EQUALS(sees(viewerConnection, distant), true);
        viewer->SetVirtualWorld(5);
        EQUALS(sees(viewerConnection, distant), false);
        EQUALS(viewerConnection->GetVirtualWorld(), 5);
        manager->DestroyEntity(distant);
        manager->DestroyEntity(bystander);
        manager->DestroyEntity(viewer);
    });
});
