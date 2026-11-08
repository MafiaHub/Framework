/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_server.h"
#include "networking/replication/interest_refresh.h"
#include "networking/replication/replication_connection.h"
#include "networking/replication/replication_manager.h"

#include <array>

MODULE(network_work, {
    IT("distributes viewer work and does not catch up missed periods after a stall", {
        using Framework::Networking::Replication::InterestRefresh;
        std::array<InterestRefresh, 512> viewers;
        std::array<int, 10> buckets {};
        int total = 0;
        for (unsigned int i = 0; i < viewers.size(); ++i) {
            EQUALS(viewers[i].Due(0, 100, i, true, true), true);
        }
        for (unsigned int now = 1; now <= 100; ++now) {
            for (unsigned int i = 0; i < viewers.size(); ++i) {
                if (viewers[i].Due(now, 100, i, false, true)) {
                    ++buckets[(now - 1) / 10];
                    ++total;
                }
            }
        }
        EQUALS(total, 512);
        for (int bucket : buckets) {
            GREATER(bucket, 0);
            LESSER(bucket, 100);
        }
        for (unsigned int i = 0; i < viewers.size(); ++i) {
            EQUALS(viewers[i].Due(1000, 100, i, false, true), true);
            EQUALS(viewers[i].Due(1000, 100, i, false, true), false);
        }
    });

    IT("refreshes urgent changes immediately and preserves zero interval behavior", {
        Framework::Networking::Replication::InterestRefresh refresh;
        EQUALS(refresh.Due(1000, 100, 7, true, true), true);
        EQUALS(refresh.Due(1000, 100, 7, false, true), false);
        EQUALS(refresh.Due(1000, 100, 7, true, false), true);
        EQUALS(refresh.Due(1000, 0, 7, false, false), false);
        EQUALS(refresh.Due(1000, 0, 7, false, true), true);
        EQUALS(refresh.Due(1000, 100, 7, false, false), true);
        // Still refresh on the next phase even if the grid generation is unchanged.
        EQUALS(refresh.Due(1100, 100, 7, false, false), true);
        EQUALS(refresh.Due(0, 100, 7, false, false), true);
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

        // Whether the connection's current interest set holds the entity.
        auto sees = [](ReplicationConnection *connection, NetworkEntity *entity) {
            DataStructures::List<MafiaNet::Replica3 *> create, destroy;
            connection->QueryReplicaList(create, destroy);
            for (unsigned int i = 0; i < create.Size(); ++i) {
                if (create[i] == entity) {
                    return true;
                }
            }
            return false;
        };
        // Moves the grid on without any refresh-forcing change: cached sets keep the old picture.
        auto rebuildQuietly = [&] {
            manager->SetInterestRebuildInterval(0);
            manager->RebuildInterest();
            manager->SetInterestRebuildInterval(kFarInterval);
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
        auto *viewer            = manager->CreateEntity(type);
        auto *nearby            = manager->CreateEntity(type);
        viewer->streaming.range = 100;
        const MafiaNet::RakNetGUID viewerGuid(93);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer);
        manager->RebuildInterest();
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        auto *connection = static_cast<ReplicationConnection *>(manager->GetConnectionByGUID(viewerGuid));
        // Query and apply, as ReplicaManager3::Update does.
        auto pass = [&] {
            connection->AutoConstructByQuery(manager, 0);
        };

        pass();
        pass();
        EQUALS(connection->HasReplicaConstructed(nearby), true);

        // A refused client destruction drops it outside the diff.
        manager->Dereference(nearby);
        manager->Reference(nearby);
        pass();
        EQUALS(connection->HasReplicaConstructed(nearby), true);
        pass();

        // Settled, and a refresh still applies.
        nearby->position.x = 1000;
        manager->SetInterestRebuildInterval(0);
        manager->RebuildInterest();
        pass();
        EQUALS(connection->HasReplicaConstructed(nearby), false);

        manager->DestroyEntity(nearby);
        manager->DestroyEntity(viewer);
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
        auto *viewer            = manager->CreateEntity(type);
        auto *leaving           = manager->CreateEntity(type);
        auto *arriving          = manager->CreateEntity(type);
        viewer->streaming.range = 100;
        arriving->position.x    = 1000;
        const MafiaNet::RakNetGUID viewerGuid(95);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer);
        manager->RebuildInterest();
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        auto *connection = static_cast<ReplicationConnection *>(manager->GetConnectionByGUID(viewerGuid));

        connection->AutoConstructByQuery(manager, 0);
        EQUALS(connection->HasReplicaConstructed(leaving), true);
        EQUALS(connection->HasReplicaConstructed(arriving), false);

        // The constructed count matches the new interest set, but its members differ.
        leaving->position.x  = 1000;
        arriving->position.x = 0;
        manager->RebuildInterest();
        connection->AutoConstructByQuery(manager, 0);
        EQUALS(connection->HasReplicaConstructed(leaving), false);
        EQUALS(connection->HasReplicaConstructed(arriving), true);

        manager->DestroyEntity(arriving);
        manager->DestroyEntity(leaving);
        manager->DestroyEntity(viewer);
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
        auto *viewer            = manager->CreateEntity(type);
        auto *distant           = manager->CreateEntity(type);
        viewer->streaming.range = 100;
        distant->position.x     = 1000;
        const MafiaNet::RakNetGUID viewerGuid(94);
        manager->SetViewer(MafiaNet::ToPeerGuid(viewerGuid), viewer);
        manager->RebuildInterest();
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, viewerGuid));
        auto *connection = static_cast<ReplicationConnection *>(manager->GetConnectionByGUID(viewerGuid));

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
        manager->OnReceive(&packet);
        EQUALS(connection->HasReplicaConstructed(distant), false);

        manager->DestroyEntity(distant);
        manager->DestroyEntity(viewer);
    });
});
