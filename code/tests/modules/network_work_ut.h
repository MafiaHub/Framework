#pragma once

#include "networking/network_server.h"
#include "networking/replication/interest_refresh.h"
#include "networking/replication/replication_connection.h"
#include "networking/replication/replication_manager.h"
#include "networking/sampled_statistics_history.h"

#include <array>

MODULE(network_work, {
    IT("samples history at most ten times per second during a packet flood", {
        Framework::Networking::NetworkServer peer;
        Framework::Networking::SampledStatisticsHistory history;
        history.SetRakPeerInterface(peer.GetPeer());
        history.SetTrackConnections(true, 0, true);
        MafiaNet::PluginInterface2 &plugin = history;
        plugin.OnNewConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, MafiaNet::RakNetGUID(77), true);
        NEQUALS(history.statistics.GetObjectIndex(77), static_cast<unsigned int>(-1));
        int samples = 0;
        for (unsigned int now = 0; now < 1000; ++now) {
            for (int packet = 0; packet < 50; ++packet) {
                samples += history.UpdateAt(now) ? 1 : 0;
            }
        }
        EQUALS(samples, 10);
        EQUALS(history.UpdateAt(1000), true);
        // A stall samples once, without replaying missed history samples.
        EQUALS(history.UpdateAt(5000), true);
        EQUALS(history.UpdateAt(5000), false);
        plugin.OnClosedConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, MafiaNet::RakNetGUID(77), MafiaNet::LCR_CONNECTION_LOST);
        EQUALS(history.statistics.GetObjectIndex(77), static_cast<unsigned int>(-1));
        history.OnRakPeerShutdown();
        EQUALS(history.UpdateAt(5001), true);
        EQUALS(history.UpdateAt(0), true);
    });

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

    IT("invalidates phased connection caches on deletion ownership and viewer world changes", {
        using namespace Framework::Networking::Replication;
        Framework::Networking::NetworkServer peer;
        auto *manager = peer.GetReplicationManager();
        manager->Init(&peer, true);
        manager->ConfigureGrid(100, -4096, 4096);
        // Keep routine work distant in the future: changes below must bypass its deadline.
        manager->SetInterestRebuildInterval(1000000);
        const auto type         = EntityRegistry::Get().Register<NetworkEntity>("Test::InterestRefresh");
        auto *viewer            = manager->CreateEntity(type);
        auto *nearby            = manager->CreateEntity(type);
        auto *distant               = manager->CreateEntity(type);
        viewer->streaming.range = 100;
        distant->position.x         = 1000;
        const MafiaNet::RakNetGUID guid(77);
        manager->SetViewer(MafiaNet::ToPeerGuid(guid), viewer);
        manager->RebuildInterest();
        ReplicationConnection connection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, guid, manager, true);
        DataStructures::List<MafiaNet::Replica3 *> create, destroy;
        auto contains = [&](NetworkEntity *entity) {
            for (unsigned int i = 0; i < create.Size(); ++i) {
                if (create[i] == entity)
                    return true;
            }
            return false;
        };
        auto query = [&] {
            create.Clear(true, _FILE_AND_LINE_);
            destroy.Clear(true, _FILE_AND_LINE_);
            connection.QueryReplicaList(create, destroy);
        };
        query();
        EQUALS(contains(nearby), true);
        EQUALS(contains(distant), false);
        manager->SetOwner(distant, MafiaNet::ToPeerGuid(guid));
        query();
        EQUALS(contains(distant), true);
        manager->DestroyEntity(nearby);
        query();
        EQUALS(contains(nearby), false);
        // Owned entities intentionally bypass world culling. Relinquish it
        // before testing a normal in-range entity's dimension change.
        distant->position.x = 10;
        manager->SetOwner(distant, MafiaNet::UNASSIGNED_PEER_GUID);
        manager->SetInterestRebuildInterval(0);
        manager->RebuildInterest();
        manager->SetInterestRebuildInterval(1000000);
        query();
        EQUALS(contains(distant), true);
        viewer->SetVirtualWorld(5);
        query();
        EQUALS(connection.GetVirtualWorld(), 5);
        EQUALS(contains(distant), false);
        manager->DestroyEntity(distant);
        manager->DestroyEntity(viewer);
    });
});
