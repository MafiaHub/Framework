/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/replication/client_entities.h"
#include "../replication_test_world.h"
#include "networking/network_server.h"
#include "networking/replication/replication_manager.h"

#include <array>
#include <chrono>
#include <cmath>
#include <thread>

// The serialize pass served across worker threads, over real loopback connections: what each
// client ends up with is all that counts, whichever thread served it.
MODULE(replication_parallel, {
    using namespace Framework::Networking::Replication;

    // The step that failed, 0 when every client saw every move. The test macros cannot run in here.
    const auto deliversEveryMoveToEveryClient = [](bool batching) {
        // Enough clients that the pass is split between workers.
        constexpr int kClients = 10;
        Framework::Networking::NetworkServer server;
        ReplicationManager *manager = server.GetReplicationManager();
        manager->Init(&server, true);
        manager->SetReplicationWorkerThreads(3);
        manager->SetTransformBatchingEnabled(batching);
        manager->ConfigureGrid(100.0f, -10000.0f, 10000.0f);
        manager->SetAutoManageConnections(false, true);
        MafiaNet::SocketDescriptor serverSocket(0, "127.0.0.1");
        if (server.GetPeer()->Startup(kClients, &serverSocket, 1) != MafiaNet::RAKNET_STARTED) {
            return 1;
        }
        server.GetPeer()->SetMaximumIncomingConnections(kClients);

        const uint32_t type = EntityRegistry::Get().Register<NetworkEntity>("Test::ParallelServe");
        NetworkEntity *npc  = TestWorld::Make(manager, type);
        npc->position       = glm::vec3(10.0f, 0.0f, 10.0f);

        server.SetOnPlayerConnectCallback([&](MafiaNet::Packet *packet) {
            server.PushReplicationConnection(packet->guid);
            NetworkEntity *avatar = TestWorld::Make(manager, type);
            avatar->ownerGUID     = MafiaNet::ToPeerGuid(packet->guid);
            manager->SetViewer(avatar->ownerGUID, avatar->GetNetworkID());
        });

        std::array<Framework::Networking::NetworkServer, kClients> clients;
        const auto port = server.GetPeer()->GetMyBoundAddress().GetPort();
        for (auto &client : clients) {
            MafiaNet::SocketDescriptor socket(0, "127.0.0.1");
            if (client.GetPeer()->Startup(1, &socket, 1) != MafiaNet::RAKNET_STARTED) {
                return 2;
            }
            client.GetReplicationManager()->Init(&client, false);
            if (client.GetPeer()->Connect("127.0.0.1", port, nullptr, 0) != MafiaNet::CONNECTION_ATTEMPT_STARTED) {
                return 3;
            }
        }

        const auto pumpUntil = [&](const auto &done) {
            for (int i = 0; i < 2500; ++i) {
                server.Update();
                server.DrainPackets();
                for (auto &client : clients) {
                    // A server peer standing in for a client: the client's update, which drains.
                    client.NetworkPeer::Update();
                }
                if (done()) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return false;
        };
        const auto everyClientHas = [&](const glm::vec3 &position) {
            for (auto &client : clients) {
                const NetworkEntity *copy = Framework::Networking::Replication::ClientEntities(*client.GetReplicationManager()).GetEntityByNetworkID(npc->GetNetworkID());
                if (!copy || glm::length(copy->position - position) > 0.01f) {
                    return false;
                }
            }
            return true;
        };

        int failed = pumpUntil([&] {
            return everyClientHas(npc->position);
        })
                         ? 0
                         : 4;
        for (int move = 1; move <= 5 && failed == 0; ++move) {
            npc->position = glm::vec3(10.0f + move, 0.0f, 10.0f - move);
            if (!pumpUntil([&] {
                    return everyClientHas(npc->position);
                })) {
                failed = 10 + move;
            }
        }

        // Destruction is broadcast, so it goes before the peers shut down and free the connections.
        TestWorld::Destroy(manager, npc);
        for (auto &client : clients) {
            client.GetPeer()->Shutdown(0);
        }
        server.GetPeer()->Shutdown(0);
        return failed;
    };

    IT("delivers every move to every client while connections are served on worker threads", {
        EQUALS(deliversEveryMoveToEveryClient(false), 0);
    });

    IT("delivers every batched move to every client while connections are served on worker threads", {
        EQUALS(deliversEveryMoveToEveryClient(true), 0);
    });
});
