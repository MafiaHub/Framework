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

#include <mafianet/bit_stream.h>
#include <mafianet/message_identifiers.h>

#include <algorithm>
#include <array>
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
                manager->DestroyEntity(entity);
            }
            manager->Clear();
        }
        MafiaNet::Connection_RM3 *Connect(uint64_t id, float x, float range) {
            static const uint32_t type = EntityRegistry::Get().Register<NetworkEntity>("Test::SerializeViewer");
            const MafiaNet::RakNetGUID guid(id);
            NetworkEntity *viewer   = manager->CreateEntity(type);
            viewer->position.x      = x;
            viewer->streaming.range = range;
            viewer->ownerGUID       = MafiaNet::ToPeerGuid(guid);
            entities.push_back(viewer);
            manager->SetViewer(MafiaNet::ToPeerGuid(guid), viewer);
            manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, guid));
            MafiaNet::Connection_RM3 *connection = manager->GetConnectionByGUID(guid);
            connection->isValidated              = true;
            return connection;
        }
        SerializeProbe *Probe(float x) {
            static const uint32_t type = EntityRegistry::Get().Register<SerializeProbe>("Test::SerializeProbe");
            auto *probe                = static_cast<SerializeProbe *>(manager->CreateEntity(type));
            probe->position.x          = x;
            entities.push_back(probe);
            return probe;
        }
        void Destroy(NetworkEntity *entity) {
            entities.erase(std::find(entities.begin(), entities.end(), entity));
            manager->DestroyEntity(entity);
        }
        void Pass(int passes = 1) {
            for (int i = 0; i < passes; ++i) {
                manager->Update();
            }
        }
    };
} // namespace NetworkWorkTest

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
        server.manager->DeallocConnection(server.manager->PopConnection(farViewer->GetRakNetGUID()));
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
        server.manager->OnReceive(&packet);
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
