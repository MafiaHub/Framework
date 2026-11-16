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
#include "networking/replication/delegation.h"
#include "networking/replication/entity_registry.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "networking/replication/world.h"

#include <glm/glm.hpp>
#include <mafianet/types.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <thread>
#include <vector>

// The replication manager with the tick on one thread and the world's users on two others, with no
// network behind it: the shape the server has once packets are handled on their own thread. Nothing
// here is timing-dependent, so it is also what a ThreadSanitizer job runs.
namespace ReplicationThreadsTest {
    using namespace Framework::Networking::Replication;

    struct Avatar final: NetworkEntity {
        static constexpr const char *kTypeName = "Test::Threads::Avatar";
    };
    struct Prop final: NetworkEntity {
        static constexpr const char *kTypeName = "Test::Threads::Prop";
        int uses                               = 0;
    };
    // Server-owned and handed to whichever player is near: the election moves it between owners.
    struct Mob final: NetworkEntity {
        static constexpr const char *kTypeName = "Test::Threads::Mob";
        const DelegationPolicy *GetDelegationPolicy() const override {
            static const DelegationPolicy policy {};
            return &policy;
        }
    };

    constexpr int kPlayers = 10;

    inline MafiaNet::RakNetGUID PlayerGuid(int player) {
        return MafiaNet::RakNetGUID(static_cast<uint64_t>(700 + player));
    }

    // What one thread using the world made and unmade, for the checks at the end.
    struct Ledger final {
        std::vector<uint64_t> alive;
        std::vector<uint64_t> destroyed;
        uint64_t created = 0;
    };

    // What a packet handler or a script does, over and over: create, write, hand over, flag, destroy.
    inline void UseTheWorld(ReplicationManager *manager, uint32_t seed, const std::atomic<bool> &stop, Ledger &ledger) {
        World &world = manager->GetWorld();
        std::mt19937 rng(seed);
        const auto pick = [&rng](size_t count) {
            return static_cast<size_t>(rng() % count);
        };
        const auto somewhere = [&rng] {
            return glm::vec3(static_cast<float>(rng() % 400), 0.0f, static_cast<float>(rng() % 400));
        };
        const auto anyPlayer = [&rng] {
            return MafiaNet::ToPeerGuid(PlayerGuid(static_cast<int>(rng() % kPlayers)));
        };
        while (!stop) {
            switch (rng() % 10) {
            case 0:
            case 1: {
                uint64_t id = 0;
                if ((rng() & 1) != 0) {
                    const Ref<Mob> mob = world.Create<Mob>();
                    mob->position      = somewhere();
                    id                 = mob->GetNetworkID();
                }
                else {
                    const Ref<Prop> prop = world.Create<Prop>();
                    prop->position       = somewhere();
                    id                   = prop->GetNetworkID();
                }
                ledger.alive.push_back(id);
                ++ledger.created;
                break;
            }
            case 2: {
                if (ledger.alive.size() > 40 || (!ledger.alive.empty() && (rng() & 3) == 0)) {
                    const size_t index = pick(ledger.alive.size());
                    const uint64_t id  = ledger.alive[index];
                    ledger.alive[index] = ledger.alive.back();
                    ledger.alive.pop_back();
                    if (world.Destroy(id)) {
                        ledger.destroyed.push_back(id);
                    }
                }
                break;
            }
            case 3: {
                // A player walks: its avatar is what the interest and the election read.
                const uint64_t avatar = manager->GetViewerId(anyPlayer());
                if (const Ref<Avatar> held = world.Write<Avatar>(avatar)) {
                    held->position = somewhere();
                    held->velocity = glm::vec3(1.0f, 0.0f, 0.0f);
                }
                break;
            }
            case 4: {
                if (!ledger.alive.empty()) {
                    if (const Ref<NetworkEntity> held = world.Write<NetworkEntity>(ledger.alive[pick(ledger.alive.size())])) {
                        held->position = somewhere();
                        StateValue value;
                        value.type   = StateValue::Type::Number;
                        value.number = static_cast<double>(rng() % 100);
                        held->state.Set((rng() & 1) != 0 ? "fuel" : "paint", value, (rng() & 3) == 0 ? StateScope::Owner : StateScope::Broadcast);
                    }
                }
                break;
            }
            case 5: {
                // Ownership by handle and by id, to a player and back to the server.
                if (!ledger.alive.empty()) {
                    const uint64_t id              = ledger.alive[pick(ledger.alive.size())];
                    const MafiaNet::PeerGuid owner = (rng() & 1) != 0 ? anyPlayer() : MafiaNet::UNASSIGNED_PEER_GUID;
                    if ((rng() & 1) != 0) {
                        manager->SetOwner(id, owner);
                    }
                    else if (const Ref<NetworkEntity> held = world.Write<NetworkEntity>(id)) {
                        held->SetOwner(owner);
                        held->ForceState();
                    }
                }
                break;
            }
            case 6: {
                // Who may see it, and where it is.
                if (!ledger.alive.empty()) {
                    if (const Ref<NetworkEntity> held = world.Write<NetworkEntity>(ledger.alive[pick(ledger.alive.size())])) {
                        if ((rng() & 1) != 0) {
                            manager->SetViewerAdmitted(*held, anyPlayer(), (rng() & 1) != 0);
                        }
                        else {
                            manager->SetStreamTarget(*held, (rng() & 3) == 0 ? anyPlayer() : MafiaNet::UNASSIGNED_PEER_GUID);
                        }
                        manager->SetVirtualWorld(held, static_cast<MafiaNet::VirtualWorldId>(rng() % 2));
                    }
                }
                break;
            }
            case 7: {
                // A respawn: a new avatar becomes the player's viewer and the old one goes.
                const MafiaNet::PeerGuid player = anyPlayer();
                const uint64_t old              = manager->GetViewerId(player);
                uint64_t replacement            = 0;
                {
                    const Ref<Avatar> avatar = world.Create<Avatar>();
                    avatar->position         = somewhere();
                    avatar->ownerGUID        = player;
                    replacement              = avatar->GetNetworkID();
                    ++ledger.created;
                    if ((rng() & 1) != 0) {
                        manager->SetViewer(player, avatar);
                        replacement = 0;
                    }
                }
                if (replacement != 0) {
                    manager->SetViewer(player, replacement);
                }
                // Another thread may have replaced it in between: then that thread destroys it, and an
                // avatar that lost such a race stays, as an entity nobody views through.
                if (old != 0 && world.Destroy(old)) {
                    ledger.destroyed.push_back(old);
                }
                break;
            }
            case 8: {
                if (!ledger.alive.empty()) {
                    const uint64_t id = ledger.alive[pick(ledger.alive.size())];
                    manager->Delegation().SetDeclined(id, anyPlayer(), (rng() & 1) != 0);
                    (void)manager->Delegation().IsPinned(id);
                }
                break;
            }
            default: {
                // Reads, several at once.
                if (ledger.alive.size() >= 2) {
                    auto [first, second] = world.Read<NetworkEntity, NetworkEntity>(ledger.alive[pick(ledger.alive.size())], ledger.alive[pick(ledger.alive.size())]);
                    if (first && second) {
                        (void)glm::distance(first->position, second->position);
                    }
                }
                break;
            }
            }
        }
    }
} // namespace ReplicationThreadsTest

MODULE(replication_threads, {
    using namespace ReplicationThreadsTest;

    IT("keeps the world whole while two threads use it and a third runs the tick", {
        Framework::Networking::NetworkServer peer;
        ReplicationManager *manager = peer.GetReplicationManager();
        manager->Init(&peer, true);
        struct Cleanup {
            ReplicationManager *manager;
            ~Cleanup() {
                manager->Clear();
            }
        } cleanup {manager};
        EntityRegistry::Get().Register<Avatar>(Avatar::kTypeName);
        EntityRegistry::Get().Register<Prop>(Prop::kTypeName);
        EntityRegistry::Get().Register<Mob>(Mob::kTypeName);
        manager->ConfigureGrid(100.0f, -1000.0f, 1000.0f);
        manager->SetReplicationWorkerThreads(3);
        manager->SetAutoSerializeInterval(0);
        manager->Delegation().SetUpdateInterval(0);
        World &world = manager->GetWorld();

        // Enough players that the interest and the pass are shared out to the workers.
        for (int player = 0; player < kPlayers; ++player) {
            const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(PlayerGuid(player));
            {
                const Ref<Avatar> avatar = world.Create<Avatar>();
                avatar->ownerGUID        = guid;
                avatar->position         = glm::vec3(static_cast<float>(player * 30), 0.0f, 0.0f);
                manager->SetViewer(guid, avatar);
            }
            TestWorld::Rm3(manager)->PushConnection(TestWorld::Rm3(manager)->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, PlayerGuid(player)));
            TestWorld::Rm3(manager)->GetConnectionByGUID(PlayerGuid(player))->isValidated = true;
        }

        // Handlers as a game hangs them: told by id, they read the entity through the world.
        std::atomic<uint64_t> tornDown {0};
        std::atomic<uint64_t> stateChanges {0};
        std::atomic<uint64_t> handovers {0};
        std::atomic<uint64_t> heldInHandler {0};
        manager->SetOnEntityDestroyed([&](uint64_t) {
            ++tornDown;
        });
        const auto stateHandler = manager->AddStateChangeHandler({}, [&](const StateChange &change) {
            ++stateChanges;
            heldInHandler += World::HeldByThisThread();
            (void)static_cast<bool>(world.Read<NetworkEntity>(change.networkId));
        });
        const auto simulatorHandler = manager->Delegation().AddSimulatorChangedHandler([&](uint64_t id, MafiaNet::PeerGuid, MafiaNet::PeerGuid, DelegationChange) {
            ++handovers;
            heldInHandler += World::HeldByThisThread();
            (void)static_cast<bool>(world.Read<NetworkEntity>(id));
        });

        std::atomic<bool> stop {false};
        std::atomic<uint64_t> ticks {0};
        std::atomic<uint64_t> heldAfterTick {0};
        // NetworkPeer::Update without the receive: the interest, the election, the bags, the pass.
        std::thread tick([&] {
            while (!stop) {
                manager->RebuildInterest();
                manager->Delegation().Update();
                manager->FlushStateBags();
                manager->Update();
                World::Settle();
                heldAfterTick += World::HeldByThisThread();
                ++ticks;
                // A tick sleeps out the rest of its interval. Without a gap this loop holds
                // ReplicaManager3's world back to back, and a creator waits its turn for seconds.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        Ledger ledgers[2];
        uint64_t heldAfterUse[2] = {0, 0};
        std::thread users[2];
        for (int user = 0; user < 2; ++user) {
            users[user] = std::thread([&, user] {
                UseTheWorld(manager, 1000u + static_cast<uint32_t>(user), stop, ledgers[user]);
                heldAfterUse[user] = World::HeldByThisThread();
            });
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
        stop = true;
        for (std::thread &user : users) {
            user.join();
        }
        tick.join();

        // One more tick on this thread, with nobody else about.
        manager->RebuildInterest();
        manager->Delegation().UpdateNow();
        manager->FlushStateBags();
        manager->Update();
        World::Settle();
        manager->RemoveStateChangeHandler(stateHandler);
        manager->Delegation().RemoveSimulatorChangedHandler(simulatorHandler);

        uint64_t created      = kPlayers;
        uint64_t destroyed    = 0;
        uint64_t unresolved   = 0;
        uint64_t stillThere   = 0;
        for (const Ledger &ledger : ledgers) {
            created += ledger.created;
            destroyed += ledger.destroyed.size();
            for (const uint64_t id : ledger.alive) {
                unresolved += world.Read<NetworkEntity>(id) ? 0 : 1;
            }
            for (const uint64_t id : ledger.destroyed) {
                stillThere += world.Read<NetworkEntity>(id) ? 1 : 0;
            }
        }
        uint64_t counted = 0;
        world.ForEach<NetworkEntity>([&counted](Ref<NetworkEntity> &) {
            ++counted;
        });
        uint64_t viewersGone = 0;
        const auto viewers   = manager->GetViewerIds();
        for (const auto &[guid, id] : viewers) {
            viewersGone += world.Read<Avatar>(id) ? 0 : 1;
        }

        EQUALS(ticks.load() > 0, true);
        EQUALS(created > static_cast<uint64_t>(kPlayers), true);
        EQUALS(stateChanges.load() > 0, true);
        EQUALS(handovers.load() > 0, true);
        EQUALS(unresolved, uint64_t {0});
        EQUALS(stillThere, uint64_t {0});
        EQUALS(counted, created - destroyed);
        EQUALS(tornDown.load(), destroyed);
        EQUALS(viewers.size(), static_cast<size_t>(kPlayers));
        EQUALS(viewersGone, uint64_t {0});
        EQUALS(heldInHandler.load(), uint64_t {0});
        EQUALS(heldAfterTick.load(), uint64_t {0});
        EQUALS(heldAfterUse[0] + heldAfterUse[1], uint64_t {0});
        EQUALS(World::HeldByThisThread(), size_t {0});
    });
});
