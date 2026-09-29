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
});
