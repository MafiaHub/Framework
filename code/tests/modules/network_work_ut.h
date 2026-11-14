/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../replication_test_world.h"
#include "networking/network_server.h"
#include "networking/replication/replication_connection.h"
#include "networking/replication/replication_manager.h"

#include <mafianet/bit_stream.h>
#include <mafianet/message_identifiers.h>

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace NetworkWorkTest {
    using namespace Framework::Networking::Replication;

    // Records what each connection is sent for this entity.
    struct SerializeProbe: NetworkEntity {
        int32_t value = 0;
        std::unordered_map<const MafiaNet::Connection_RM3 *, int> stateSends;
        int emptyVisits = 0;
        void SerializeFields(FieldSerializer &fields) override {
            fields.Field(value);
        }
        void OnSerializeTransmission(MafiaNet::BitStream *, MafiaNet::Connection_RM3 *to, MafiaNet::BitSize_t bits[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::Time) override {
            if (bits[1] > 0) {
                ++stateSends[to];
            }
            if (std::all_of(bits, bits + MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS, [](MafiaNet::BitSize_t channel) {
                    return channel == 0;
                })) {
                ++emptyVisits;
            }
        }
        void Forget() {
            stateSends.clear();
            emptyVisits = 0;
        }
    };

    // A server with fake validated connections, each owning the avatar it views from.
    struct Server {
        Framework::Networking::NetworkServer peer;
        ReplicationManager *manager = nullptr;
        std::vector<NetworkEntity *> entities;

        Server() {
            manager = peer.GetReplicationManager();
            manager->Init(&peer, true);
            manager->SetAutoSerializeInterval(0);
            manager->ConfigureGrid(100, -4096, 4096);
        }
        ~Server() {
            for (NetworkEntity *entity : entities) {
                TestWorld::Destroy(manager, entity);
            }
            manager->Clear();
        }
        MafiaNet::Connection_RM3 *Connect(uint64_t id, float x, float range) {
            static const uint32_t type = EntityRegistry::Get().Register<NetworkEntity>("Test::SerializeViewer");
            const MafiaNet::RakNetGUID guid(id);
            NetworkEntity *viewer   = TestWorld::Make(manager, type);
            viewer->position.x      = x;
            viewer->streaming.range = range;
            viewer->ownerGUID       = MafiaNet::ToPeerGuid(guid);
            entities.push_back(viewer);
            manager->SetViewer(MafiaNet::ToPeerGuid(guid), viewer->GetNetworkID());
            TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, guid));
            MafiaNet::Connection_RM3 *connection = TestWorld::Rm3(manager)->GetConnectionByGUID(guid);
            connection->isValidated              = true;
            return connection;
        }
        SerializeProbe *Probe(float x) {
            static const uint32_t type = EntityRegistry::Get().Register<SerializeProbe>("Test::SerializeProbe");
            auto *probe                = static_cast<SerializeProbe *>(TestWorld::Make(manager, type));
            probe->position.x          = x;
            entities.push_back(probe);
            return probe;
        }
        void Destroy(NetworkEntity *entity) {
            entities.erase(std::find(entities.begin(), entities.end(), entity));
            TestWorld::Destroy(manager, entity);
        }
        void Pass(int passes = 1) {
            for (int i = 0; i < passes; ++i) {
                manager->Update();
            }
        }
    };
} // namespace NetworkWorkTest

// Rides another entity, which a viewer has to be shown with it.
class ChurnRider final: public Framework::Networking::Replication::NetworkEntity {
  public:
    uint64_t mount = 0;
    uint64_t GetInterestDependency() override {
        return mount;
    }
};

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
                NetworkEntity *avatar = TestWorld::Make(manager, type);
                NetworkEntity *mover  = TestWorld::Make(manager, type);
                avatar->position.x    = i * 1000.0f;
                mover->position       = glm::vec3(i * 1000.0f, 0.0f, 500.0f);
                entities.push_back(avatar);
                entities.push_back(mover);
                movers.push_back(mover);
                const MafiaNet::RakNetGUID guid(1000 + i);
                manager->SetViewer(MafiaNet::ToPeerGuid(guid), avatar->GetNetworkID());
                TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, guid));
                connections.push_back(static_cast<ReplicationConnection *>(TestWorld::Rm3(manager)->GetConnectionByGUID(guid)));
            }
        }
        ~Turns() {
            for (auto *entity : entities) {
                TestWorld::Destroy(manager, entity);
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
        auto *spawned = TestWorld::Make(turns.manager, turns.entities.front()->GetTypeId());
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
        auto *viewer               = TestWorld::Make(manager, type);
        auto *bystander            = TestWorld::Make(manager, type);
        auto *nearby               = TestWorld::Make(manager, type);
        auto *distant              = TestWorld::Make(manager, type);
        viewer->streaming.range    = 100;
        bystander->streaming.range = 100;
        distant->position.x        = 1000;
        const MafiaNet::RakNetGUID viewerGuid(77), bystanderGuid(78);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer->GetNetworkID());
        manager->SetViewer(MafiaNet::ToPeerGuid(bystanderGuid), bystander->GetNetworkID());
        manager->RebuildInterest();
        TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, bystanderGuid));
        auto *viewerConnection    = static_cast<ReplicationConnection *>(TestWorld::Rm3(manager)->GetConnectionByGUID(viewerGuid));
        auto *bystanderConnection = static_cast<ReplicationConnection *>(TestWorld::Rm3(manager)->GetConnectionByGUID(bystanderGuid));

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
        distant->SetOwner(MafiaNet::ToPeerGuid(viewerGuid));
        EQUALS(sees(viewerConnection, distant), true);
        EQUALS(sees(viewerConnection, nearby), false);
        EQUALS(sees(bystanderConnection, nearby), true);

        // A destruction reaches every viewer before its phase.
        TestWorld::Destroy(manager, nearby);
        EQUALS(sees(bystanderConnection, nearby), false);

        // Owned entities bypass world culling, so hand it back before testing a dimension change.
        distant->position.x = 10;
        distant->SetOwner(MafiaNet::UNASSIGNED_PEER_GUID);
        rebuildQuietly();
        EQUALS(sees(viewerConnection, distant), true);
        viewer->SetVirtualWorld(5);
        EQUALS(sees(viewerConnection, distant), false);
        EQUALS(viewerConnection->GetVirtualWorld(), 5);
        TestWorld::Destroy(manager, distant);
        TestWorld::Destroy(manager, bystander);
        TestWorld::Destroy(manager, viewer);
    });

    IT("adds a creation and drops a destruction without refreshing anyone", {
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
        constexpr uint32_t kFarInterval = 1000000;
        manager->SetInterestRebuildInterval(kFarInterval);
        const auto type      = EntityRegistry::Get().Register<NetworkEntity>("Test::InterestChurn");
        const auto budgeted  = EntityRegistry::Get().Register<NetworkEntity>("Test::InterestChurnBudgeted");
        const auto riderType = EntityRegistry::Get().Register<ChurnRider>("Test::InterestChurnRider");
        manager->SetInterestBudget(budgeted, 4);
        auto *viewer            = TestWorld::Make(manager, type);
        auto *mover             = TestWorld::Make(manager, type);
        auto *rider             = static_cast<ChurnRider *>(TestWorld::Make(manager, riderType));
        viewer->streaming.range = 100;
        mover->position.x       = 50;
        rider->position.x       = 30;
        const MafiaNet::RakNetGUID viewerGuid(91);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer->GetNetworkID());
        manager->RebuildInterest();
        TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        auto *connection = static_cast<ReplicationConnection *>(TestWorld::Rm3(manager)->GetConnectionByGUID(viewerGuid));
        auto sees        = [connection](NetworkEntity *entity) {
            DataStructures::List<MafiaNet::Replica3 *> create, destroy;
            connection->QueryReplicaList(create, destroy);
            return connection->IsRelevant(entity);
        };
        EQUALS(sees(mover), true);
        EQUALS(sees(rider), true);

        // The mover leaves quietly, so a recompute would show by dropping it.
        mover->position.x = 2000;
        manager->RebuildInterest();

        auto *spawned       = TestWorld::Make(manager, type);
        auto *farAway       = TestWorld::Make(manager, type);
        spawned->position.x = 20;
        farAway->position.x = 3000;
        manager->RebuildInterest();
        EQUALS(sees(spawned), true);
        EQUALS(sees(farAway), false);
        EQUALS(sees(mover), true);

        // Something already shown that comes to ride a creation brings it, however far it is.
        auto *mount       = TestWorld::Make(manager, type);
        mount->position.x = 3000;
        rider->mount      = mount->GetNetworkID();
        manager->RebuildInterest();
        EQUALS(sees(mount), true);
        EQUALS(sees(mover), true);

        // Gone before the rebuild that would have handed it out.
        TestWorld::Destroy(manager, TestWorld::Make(manager, type));
        manager->RebuildInterest();
        EQUALS(sees(mover), true);

        TestWorld::Destroy(manager, spawned);
        EQUALS(sees(spawned), false);
        EQUALS(sees(mover), true);

        // A budgeted creation in reach has to be ranked: that viewer recomputes, and the quiet move shows.
        auto *npc       = TestWorld::Make(manager, budgeted);
        npc->position.x = 10;
        manager->RebuildInterest();
        EQUALS(sees(npc), true);
        EQUALS(sees(mover), false);

        rider->mount = 0;
        TestWorld::Destroy(manager, npc);
        TestWorld::Destroy(manager, mount);
        TestWorld::Destroy(manager, rider);
        TestWorld::Destroy(manager, farAway);
        TestWorld::Destroy(manager, mover);
        TestWorld::Destroy(manager, viewer);
    });

    IT("re-runs the replica diff after a change it did not make", {
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
        // Only urgent refreshes run.
        constexpr uint32_t kFarInterval = 1000000;
        manager->SetInterestRebuildInterval(kFarInterval);
        const auto type         = EntityRegistry::Get().Register<NetworkEntity>("Test::QueryDiff");
        auto *viewer            = TestWorld::Make(manager, type);
        auto *nearby            = TestWorld::Make(manager, type);
        viewer->streaming.range = 100;
        const MafiaNet::RakNetGUID viewerGuid(93);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer->GetNetworkID());
        manager->RebuildInterest();
        TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        auto *connection = static_cast<ReplicationConnection *>(TestWorld::Rm3(manager)->GetConnectionByGUID(viewerGuid));
        // Query and apply, as ReplicaManager3::Update does.
        auto pass = [&] {
            connection->AutoConstructByQuery(TestWorld::Rm3(manager), 0);
        };

        pass();
        pass();
        EQUALS(connection->HasReplicaConstructed(nearby), true);

        // A refused client destruction drops it outside the diff.
        TestWorld::Rm3(manager)->Dereference(nearby);
        TestWorld::Rm3(manager)->Reference(nearby);
        pass();
        EQUALS(connection->HasReplicaConstructed(nearby), true);
        pass();

        // Settled, and a refresh still applies.
        nearby->position.x = 1000;
        manager->SetInterestRebuildInterval(0);
        manager->RebuildInterest();
        pass();
        EQUALS(connection->HasReplicaConstructed(nearby), false);

        TestWorld::Destroy(manager, nearby);
        TestWorld::Destroy(manager, viewer);
    });

    IT("swaps one replica for another in a single refresh", {
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
        const auto type         = EntityRegistry::Get().Register<NetworkEntity>("Test::QueryDiffSwap");
        auto *viewer            = TestWorld::Make(manager, type);
        auto *leaving           = TestWorld::Make(manager, type);
        auto *arriving          = TestWorld::Make(manager, type);
        viewer->streaming.range = 100;
        arriving->position.x    = 1000;
        const MafiaNet::RakNetGUID viewerGuid(95);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer->GetNetworkID());
        manager->RebuildInterest();
        TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        auto *connection = static_cast<ReplicationConnection *>(TestWorld::Rm3(manager)->GetConnectionByGUID(viewerGuid));

        connection->AutoConstructByQuery(TestWorld::Rm3(manager), 0);
        EQUALS(connection->HasReplicaConstructed(leaving), true);
        EQUALS(connection->HasReplicaConstructed(arriving), false);

        // The constructed count matches the new interest set, but its members differ.
        leaving->position.x  = 1000;
        arriving->position.x = 0;
        manager->RebuildInterest();
        connection->AutoConstructByQuery(TestWorld::Rm3(manager), 0);
        EQUALS(connection->HasReplicaConstructed(leaving), false);
        EQUALS(connection->HasReplicaConstructed(arriving), true);

        TestWorld::Destroy(manager, arriving);
        TestWorld::Destroy(manager, leaving);
        TestWorld::Destroy(manager, viewer);
    });

    IT("refuses replica constructions from a client", {
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
        const auto type         = EntityRegistry::Get().Register<NetworkEntity>("Test::ClientConstruction");
        auto *viewer            = TestWorld::Make(manager, type);
        auto *distant           = TestWorld::Make(manager, type);
        viewer->streaming.range = 100;
        distant->position.x     = 1000;
        const MafiaNet::RakNetGUID viewerGuid(94);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer->GetNetworkID());
        manager->RebuildInterest();
        TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        auto *connection = static_cast<ReplicationConnection *>(TestWorld::Rm3(manager)->GetConnectionByGUID(viewerGuid));

        // Claims the client already holds an entity outside its interest.
        MafiaNet::BitStream bs;
        bs.Write(static_cast<MafiaNet::MessageID>(ID_REPLICA_MANAGER_CONSTRUCTION));
        bs.Write(static_cast<MafiaNet::WorldId>(0));
        bs.Write(static_cast<uint16_t>(1));
        const MafiaNet::BitSize_t entry = bs.GetWriteOffset();
        bs.Write(entry);
        bs.Write(distant->GetNetworkID());
        bs.Write(true);
        bs.AlignWriteToByteBoundary();
        bs.Write(viewerGuid);
        bs.Write(bs.GetWriteOffset());
        const MafiaNet::BitSize_t end = bs.GetWriteOffset();
        bs.SetWriteOffset(entry);
        bs.Write(end);
        bs.SetWriteOffset(end);
        bs.Write(false);
        bs.AlignWriteToByteBoundary();
        bs.Write(static_cast<uint16_t>(0));

        MafiaNet::Packet packet {};
        packet.data   = bs.GetData();
        packet.length = bs.GetNumberOfBytesUsed();
        packet.guid   = viewerGuid;
        TestWorld::OnReceive(manager, &packet);
        EQUALS(connection->HasReplicaConstructed(distant), false);

        TestWorld::Destroy(manager, distant);
        TestWorld::Destroy(manager, viewer);
    });

    IT("lets a viewer join after earlier passes consumed re-evaluations", {
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
        const auto type         = EntityRegistry::Get().Register<NetworkEntity>("Test::LateJoin");
        auto *viewer            = TestWorld::Make(manager, type);
        viewer->streaming.range = 100;
        manager->RebuildInterest();
        manager->Update();
        // Queued and consumed before the viewer's connection exists.
        auto *nearby = TestWorld::Make(manager, type);
        manager->RebuildInterest();
        manager->Update();
        EQUALS(manager->ReevaluationBase() > 0, true);

        const MafiaNet::RakNetGUID viewerGuid(97);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer->GetNetworkID());
        TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        auto *connection = static_cast<ReplicationConnection *>(TestWorld::Rm3(manager)->GetConnectionByGUID(viewerGuid));
        connection->AutoConstructByQuery(TestWorld::Rm3(manager), 0);
        EQUALS(connection->HasReplicaConstructed(nearby), true);

        TestWorld::Destroy(manager, nearby);
        TestWorld::Destroy(manager, viewer);
    });

    IT("sends a changed field to every viewer and nothing per viewer for an unchanged entity", {
        NetworkWorkTest::Server server;
        MafiaNet::Connection_RM3 *connections[] = {server.Connect(201, 0, 1000), server.Connect(202, 0, 1000), server.Connect(203, 0, 1000)};

        auto *changing = server.Probe(0);
        auto *idle     = server.Probe(0);
        // Owned by the first connection, which is never sent it.
        auto *owned      = server.Probe(0);
        owned->ownerGUID = MafiaNet::ToPeerGuid(connections[0]->GetRakNetGUID());
        server.manager->RebuildInterest();
        server.Pass(3);
        for (auto *probe : {changing, idle, owned}) {
            probe->Forget();
        }

        changing->value = 5;
        owned->value    = 5;
        server.Pass();
        for (auto *connection : connections) {
            EQUALS(connection->HasReplicaConstructed(idle), true);
            EQUALS(changing->stateSends[connection], 1);
        }
        EQUALS(owned->stateSends[connections[0]], 0);
        EQUALS(owned->stateSends[connections[1]], 1);
        EQUALS(owned->stateSends[connections[2]], 1);
        EQUALS(idle->stateSends.empty(), true);
        EQUALS(idle->emptyVisits, 0);

        server.Pass();
        EQUALS(changing->stateSends[connections[2]], 1);
        EQUALS(idle->emptyVisits, 0);
    });

    IT("holds a change back from a viewer in another virtual world until it is back", {
        NetworkWorkTest::Server server;
        MafiaNet::Connection_RM3 *viewer = server.Connect(211, 0, 1000);
        auto *probe                      = server.Probe(0);
        server.manager->RebuildInterest();
        server.Pass(3);
        probe->Forget();

        // The viewer's interest has not refreshed, so the entity is still constructed there.
        probe->SetVirtualWorld(5);
        probe->value = 7;
        server.Pass();
        EQUALS(viewer->HasReplicaConstructed(probe), true);
        EQUALS(probe->stateSends[viewer], 0);

        probe->SetVirtualWorld(0);
        server.Pass();
        EQUALS(probe->stateSends[viewer], 1);
    });

    IT("follows an entity out of a viewer's interest and back, and past a closed connection", {
        NetworkWorkTest::Server server;
        MafiaNet::Connection_RM3 *nearViewer = server.Connect(221, 0, 200);
        MafiaNet::Connection_RM3 *farViewer  = server.Connect(222, 0, 3000);
        auto *probe                          = server.Probe(50);
        auto *stay                           = server.Probe(50);
        server.manager->RebuildInterest();
        server.Pass(3);

        probe->position.x = 1000;
        server.manager->RebuildInterest();
        server.Pass();
        EQUALS(nearViewer->HasReplicaConstructed(probe), false);
        probe->Forget();
        probe->value = 1;
        server.Pass();
        EQUALS(probe->stateSends[nearViewer], 0);
        EQUALS(probe->stateSends[farViewer], 1);

        probe->position.x = 50;
        server.manager->RebuildInterest();
        server.Pass();
        EQUALS(nearViewer->HasReplicaConstructed(probe), true);
        probe->Forget();
        probe->value = 2;
        server.Pass();
        EQUALS(probe->stateSends[nearViewer], 1);
        EQUALS(probe->stateSends[farViewer], 1);

        // The closed connection is only a map key from here on.
        TestWorld::Rm3(server.manager)->DeallocConnection(TestWorld::Rm3(server.manager)->PopConnection(farViewer->GetRakNetGUID()));
        probe->value = 3;
        server.Pass();
        EQUALS(probe->stateSends[nearViewer], 2);
        EQUALS(probe->stateSends[farViewer], 1);

        server.Destroy(probe);
        stay->Forget();
        stay->value = 9;
        server.Pass();
        EQUALS(stay->stateSends[nearViewer], 1);
    });

    IT("takes a viewer out of an entity's audience when the server refuses its destruction", {
        NetworkWorkTest::Server server;
        MafiaNet::Connection_RM3 *viewer = server.Connect(231, 0, 200);
        MafiaNet::Connection_RM3 *other  = server.Connect(232, 0, 3000);
        auto *probe                      = server.Probe(50);
        server.manager->RebuildInterest();
        server.Pass(3);

        // What a client's DestroyEntity sends for an entity it does not own. The server refuses it, and
        // ReplicaManager3 un-constructs the entity on that connection alone.
        MafiaNet::BitStream destruction;
        destruction.Write(static_cast<MafiaNet::MessageID>(ID_REPLICA_MANAGER_CONSTRUCTION));
        destruction.Write(static_cast<MafiaNet::WorldId>(0));
        destruction.Write(static_cast<uint16_t>(0));
        destruction.Write(static_cast<uint16_t>(1));
        destruction.Write(probe->GetNetworkID());
        destruction.Write(static_cast<MafiaNet::BitSize_t>(0));
        destruction.Write(viewer->GetRakNetGUID());
        MafiaNet::Packet packet {};
        packet.data   = destruction.GetData();
        packet.length = destruction.GetNumberOfBytesUsed();
        packet.guid   = viewer->GetRakNetGUID();
        TestWorld::OnReceive(server.manager, &packet);
        EQUALS(viewer->HasReplicaConstructed(probe), false);
        server.Pass();
        EQUALS(viewer->HasReplicaConstructed(probe), true);

        probe->position.x = 1000;
        server.manager->RebuildInterest();
        server.Pass();
        EQUALS(viewer->HasReplicaConstructed(probe), false);
        probe->Forget();
        probe->value = 1;
        server.Pass();
        EQUALS(probe->stateSends[viewer], 0);
        EQUALS(probe->stateSends[other], 1);
    });

    IT("brings a viewer constructed between serialize passes up to a value that changed back", {
        NetworkWorkTest::Server server;
        server.Connect(241, 0, 1000);
        auto *probe  = server.Probe(0);
        probe->value = 1;
        server.manager->RebuildInterest();
        server.Pass(3);

        // Constructed with 2 on a pass with no serialize due; the value is 1 again before the next one.
        server.manager->SetAutoSerializeInterval(60000);
        probe->value                   = 2;
        MafiaNet::Connection_RM3 *late = server.Connect(242, 0, 1000);
        server.Pass();
        EQUALS(late->HasReplicaConstructed(probe), true);
        probe->Forget();
        probe->value = 1;
        server.manager->SetAutoSerializeInterval(0);
        server.Pass();
        EQUALS(probe->stateSends[late], 1);
    });
});
