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
#include "alloc_counter.h"
#include "networking/network_server.h"
#include "networking/replication/replication_manager.h"
#include "networking/replication/state_bag.h"
#include "networking/rpc/state_bag_sync.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Which connections a state bag change reaches, over real loopback connections: the routing in
// ReplicationManager::FlushStateBags asks Connection_RM3::HasReplicaConstructed, which only a real
// construction handshake answers, so what each client ends up holding is what is asserted.
MODULE(state_bag_routing, {
    using Framework::Networking::NetworkServer;
    using Framework::Networking::Replication::EntityRegistry;
    using Framework::Networking::Replication::NetworkEntity;
    using Framework::Networking::Replication::ReplicationManager;
    using Framework::Networking::Replication::StateScope;
    using Framework::Networking::Replication::StateValue;
    using Framework::Networking::RPC::StateBagSync;

    const auto text = [](const std::string &value) {
        StateValue out;
        out.type = StateValue::Type::String;
        out.text = value;
        return out;
    };

    // A server with one shared NPC and one avatar per client, owned by that client. The test macros
    // cannot run inside the helpers, so they answer with a bool and the case asserts on it.
    struct Rig {
        NetworkServer server;
        ReplicationManager *manager = nullptr;
        std::vector<std::unique_ptr<NetworkServer>> clients;
        std::vector<NetworkEntity *> avatars; // by client index
        NetworkEntity *npc = nullptr;
        uint32_t type      = 0;

        ~Rig() {
            // Destruction is broadcast, so it goes before the peers shut down and free the connections.
            if (npc != nullptr) {
                TestWorld::Destroy(manager, npc);
            }
            for (auto &client : clients) {
                client->GetPeer()->Shutdown(0);
            }
            server.GetPeer()->Shutdown(0);
        }

        bool Setup(int clientCount, const char *typeName) {
            manager = server.GetReplicationManager();
            manager->Init(&server, true);
            manager->ConfigureGrid(100.0f, -10000.0f, 10000.0f);
            manager->SetAutoManageConnections(false, true);
            MafiaNet::SocketDescriptor serverSocket(0, "127.0.0.1");
            if (server.GetPeer()->Startup(static_cast<unsigned>(clientCount), &serverSocket, 1) != MafiaNet::RAKNET_STARTED) {
                return false;
            }
            server.GetPeer()->SetMaximumIncomingConnections(static_cast<unsigned short>(clientCount));

            type     = EntityRegistry::Get().Register<NetworkEntity>(typeName);
            npc      = TestWorld::Make(manager, type);
            npc->position = glm::vec3(10.0f, 0.0f, 10.0f);

            std::vector<std::pair<MafiaNet::PeerGuid, NetworkEntity *>> avatarByGuid;
            server.SetOnPlayerConnectCallback([&](MafiaNet::Packet *packet) {
                server.PushReplicationConnection(packet->guid);
                NetworkEntity *avatar = TestWorld::Make(manager, type);
                avatar->ownerGUID     = MafiaNet::ToPeerGuid(packet->guid);
                manager->SetViewer(avatar->ownerGUID, avatar->GetNetworkID());
                avatarByGuid.emplace_back(avatar->ownerGUID, avatar);
            });

            const auto port = server.GetPeer()->GetMyBoundAddress().GetPort();
            for (int i = 0; i < clientCount; ++i) {
                auto client = std::make_unique<NetworkServer>();
                MafiaNet::SocketDescriptor socket(0, "127.0.0.1");
                if (client->GetPeer()->Startup(1, &socket, 1) != MafiaNet::RAKNET_STARTED) {
                    return false;
                }
                client->GetReplicationManager()->Init(client.get(), false);
                if (client->GetPeer()->Connect("127.0.0.1", port, nullptr, 0) != MafiaNet::CONNECTION_ATTEMPT_STARTED) {
                    return false;
                }
                clients.push_back(std::move(client));
            }

            if (!PumpUntil([&] {
                    return avatarByGuid.size() == clients.size();
                })) {
                return false;
            }
            for (auto &client : clients) {
                const auto guid = MafiaNet::ToPeerGuid(client->GetPeer()->GetMyGUID());
                NetworkEntity *found = nullptr;
                for (const auto &[ownerGuid, avatar] : avatarByGuid) {
                    if (ownerGuid == guid) {
                        found = avatar;
                    }
                }
                if (found == nullptr) {
                    return false;
                }
                avatars.push_back(found);
            }
            // Every client holds the NPC and every avatar before a case writes anything: a key written
            // earlier would travel in the construction seed, not through the flush under test.
            return PumpUntil([&] {
                for (int i = 0; i < static_cast<int>(clients.size()); ++i) {
                    if (Copy(i, npc) == nullptr) {
                        return false;
                    }
                    for (auto *avatar : avatars) {
                        if (Copy(i, avatar) == nullptr) {
                            return false;
                        }
                    }
                }
                return true;
            });
        }

        void Pump() {
            server.Update();
            for (auto &client : clients) {
                client->Update();
            }
        }

        bool PumpUntil(const std::function<bool()> &done) {
            for (int i = 0; i < 2500; ++i) {
                Pump();
                if (done()) {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return false;
        }

        void PumpFor(int milliseconds) {
            for (int elapsed = 0; elapsed < milliseconds; elapsed += 2) {
                Pump();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        NetworkEntity *Copy(int client, NetworkEntity *entity) {
            return Framework::Networking::Replication::ClientEntities(*clients[static_cast<size_t>(client)]->GetReplicationManager()).GetEntityByNetworkID(entity->GetNetworkID());
        }

        const StateValue *Seen(int client, NetworkEntity *entity, const std::string &key) {
            NetworkEntity *copy = Copy(client, entity);
            return copy != nullptr ? copy->state.Get(key) : nullptr;
        }

        bool EveryClientHas(NetworkEntity *entity, const std::string &key, const std::string &value) {
            for (int i = 0; i < static_cast<int>(clients.size()); ++i) {
                const StateValue *seen = Seen(i, entity, key);
                if (seen == nullptr || seen->text != value) {
                    return false;
                }
            }
            return true;
        }
    };

    IT("sends a broadcast key to every connection that has the entity", {
        Rig rig;
        EQUALS(rig.Setup(3, "Test::StateBagRouting"), true);

        rig.npc->state.Set("job", text("blacksmith"), StateScope::Broadcast);
        EQUALS(rig.PumpUntil([&] {
            return rig.EveryClientHas(rig.npc, "job", "blacksmith");
        }), true);
    });

    IT("sends an owner key only to the owner", {
        Rig rig;
        EQUALS(rig.Setup(3, "Test::StateBagRouting"), true);

        rig.avatars[0]->state.Set("cuffed", text("yes"), StateScope::Owner);
        EQUALS(rig.PumpUntil([&] {
            return rig.Seen(0, rig.avatars[0], "cuffed") != nullptr;
        }), true);

        rig.PumpFor(200);
        EQUALS(rig.Seen(1, rig.avatars[0], "cuffed") == nullptr, true);
        EQUALS(rig.Seen(2, rig.avatars[0], "cuffed") == nullptr, true);
    });

    IT("revokes a key from connections that leave its audience", {
        Rig rig;
        EQUALS(rig.Setup(3, "Test::StateBagRouting"), true);

        rig.avatars[0]->state.Set("job", text("blacksmith"), StateScope::Broadcast);
        EQUALS(rig.PumpUntil([&] {
            return rig.EveryClientHas(rig.avatars[0], "job", "blacksmith");
        }), true);

        rig.avatars[0]->state.Set("job", text("blacksmith"), StateScope::Owner);
        EQUALS(rig.PumpUntil([&] {
            return rig.Seen(1, rig.avatars[0], "job") == nullptr && rig.Seen(2, rig.avatars[0], "job") == nullptr;
        }), true);
        EQUALS(rig.Seen(0, rig.avatars[0], "job") != nullptr, true);
    });

    IT("removes a key everywhere", {
        Rig rig;
        EQUALS(rig.Setup(3, "Test::StateBagRouting"), true);

        rig.npc->state.Set("job", text("blacksmith"), StateScope::Broadcast);
        EQUALS(rig.PumpUntil([&] {
            return rig.EveryClientHas(rig.npc, "job", "blacksmith");
        }), true);

        rig.npc->state.Remove("job");
        EQUALS(rig.PumpUntil([&] {
            return rig.Seen(0, rig.npc, "job") == nullptr && rig.Seen(1, rig.npc, "job") == nullptr && rig.Seen(2, rig.npc, "job") == nullptr;
        }), true);
    });

    IT("chunks more changes than one payload carries", {
        Rig rig;
        EQUALS(rig.Setup(3, "Test::StateBagRouting"), true);

        // Three bags, since one holds at most 128 keys; all written before the next flush.
        NetworkEntity *bags[3] = {rig.npc, rig.avatars[0], rig.avatars[1]};
        const int total        = StateBagSync::kMaxChanges + 10;
        for (int n = 0; n < total; ++n) {
            bags[n % 3]->state.Set("k" + std::to_string(n), text("v" + std::to_string(n)), StateScope::Broadcast);
        }

        EQUALS(rig.PumpUntil([&] {
            for (int n = 0; n < total; ++n) {
                if (!rig.EveryClientHas(bags[n % 3], "k" + std::to_string(n), "v" + std::to_string(n))) {
                    return false;
                }
            }
            return true;
        }), true);
    });

    // The allocations one flush of 20 broadcast keys makes, 0 when the rig did not come up.
    const auto flushAllocations = [&](int clientCount) -> uint64_t {
        Rig rig;
        if (!rig.Setup(clientCount, "Test::StateBagRouting")) {
            return 0;
        }
        for (int n = 0; n < 20; ++n) {
            rig.npc->state.Set("key" + std::to_string(n), text(std::string(200, static_cast<char>('a' + n))), StateScope::Broadcast);
        }

        FrameworkTests::AllocationScope scope;
        rig.manager->FlushStateBags();
        return scope.Count();
    };

    IT("flushes a broadcast change without allocating per viewer", {
        const uint64_t three = flushAllocations(3);
        const uint64_t six   = flushAllocations(6);
        NEQUALS(three, uint64_t(0));
        // One copy of the key and of the value per change into the shared pool, plus overhead that
        // depends on the standard library (58 in all with MSVC, 46 with libc++). A copy per viewer
        // would be 2 * 20 * 3 on its own.
        LESSER(three, uint64_t(2 * 20 + 40));
        // Twice the viewers must not cost a copy of each change more: sending to a connection costs a
        // fixed handful of allocations (the stream and the RPC signal), whatever the changes are.
        // 9 per connection with MSVC, 6 with libc++; a copy of each change would be 2 * 20.
        LESSER(six, three + 3 * 16);
    });
});
