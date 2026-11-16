/*
 * MafiaHub source-available license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE.txt file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_client.h"
#include "networking/network_server.h"
#include "networking/packet_thread.h"
#include "networking/replication/client_entities.h"
#include "networking/replication/entity_registry.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "networking/replication/world.h"
#include "networking/rpc/rpc_identifier.h"

#include <mafianet/bit_stream.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

// The packet thread against the replication pass with no scripting engine anywhere: a bare NetworkServer, a
// PacketThread draining it, a thread ticking it with replication workers, and clients on loopback. What a
// ThreadSanitizer run can instrument end to end, which a whole server (libnode is prebuilt) is not.
namespace PacketThreadTest {
    using Clock = std::chrono::steady_clock;
    using Framework::Networking::NetworkClient;
    using Framework::Networking::NetworkServer;
    using Framework::Networking::Sender;
    using Framework::Networking::Replication::World;

    // Owned by its client, which writes `counter` (the state channel) and its pose (a transform batch). `lastStep`
    // and `outOfOrder` are the server's own: what the RPC handler keeps of the steps it was sent.
    struct Avatar final: Framework::Networking::Replication::NetworkEntity {
        static constexpr const char *kTypeName = "Test::PacketThreadBare::Avatar";
        uint32_t counter                       = 0;
        uint32_t lastStep                      = 0;
        uint32_t outOfOrder                    = 0;
        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(counter);
        }
    };

    struct Step {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Test::PacketThreadBare::Step");
        uint32_t sequence                 = 0;
        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, sequence);
        }
    };

    struct Player final {
        NetworkClient net {"127.0.0.1"};
        uint32_t sent = 0;

        Player() {
            net.SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);
            net.GetReplicationManager()->SetTransformBatchingEnabled(true);
        }
        MafiaNet::PeerGuid Guid() const {
            return MafiaNet::ToPeerGuid(net.GetPeer()->GetMyGUID());
        }
        Avatar *Own() {
            Avatar *own = nullptr;
            Framework::Networking::Replication::ClientEntities(*net.GetReplicationManager()).ForEach<Avatar>([&](Avatar *avatar) {
                if (avatar->ownerGUID == Guid()) {
                    own = avatar;
                }
            });
            return own;
        }
    };

    struct Seen {
        uint32_t counter    = 0;
        float x             = 0.0f;
        uint32_t lastStep   = 0;
        uint32_t outOfOrder = 0;
    };
} // namespace PacketThreadTest

MODULE(packet_drain, {
    using namespace PacketThreadTest;
    using namespace std::chrono_literals;

    IT("handles packets and applies inbound state beside a ticking pass, with replication workers", {
        // Eight connections: where the pass starts handing them to its workers.
        constexpr int kPlayers = 8;
        Framework::Networking::Replication::EntityRegistry::Get().Register<Avatar>(Avatar::kTypeName);

        auto server = std::make_unique<NetworkServer>();
        Framework::Networking::AdmissionSettings admission;
        admission.pendingConnections           = 16;
        admission.pendingConnectionsPerAddress = 16;
        int32_t port                           = 0;
        std::mt19937 random(static_cast<unsigned int>(Clock::now().time_since_epoch().count()));
        for (int attempt = 0; attempt < 8 && port == 0; ++attempt) {
            const int32_t candidate = 42000 + static_cast<int32_t>(random() % 20000);
            if (server->Init("127.0.0.1", candidate, kPlayers, "", admission)) {
                port = candidate;
            }
            else {
                server = std::make_unique<NetworkServer>();
            }
        }
        NEQUALS(port, 0);
        if (port == 0) {
            break;
        }
        auto *replication = server->GetReplicationManager();
        World &world      = replication->GetWorld();
        server->SetBuildToken(Framework::Networking::NetworkPeer::kBuildVerificationDisabledToken);
        replication->SetTransformBatchingEnabled(true);
        replication->SetReplicationWorkerThreads(2);
        replication->SetAutoSerializeInterval(16);

        // Everything below the registration runs on the packet thread.
        std::mutex mutex;
        std::vector<std::thread::id> handlerThreads;
        std::atomic<uint32_t> handled {0};
        server->SetOnClientAuthenticatedCallback([&](MafiaNet::RakNetGUID guid) {
            const auto peer = MafiaNet::ToPeerGuid(guid);
            {
                const auto avatar = world.Create<Avatar>();
                replication->SetOwner(avatar, peer);
                replication->SetViewer(peer, avatar);
            }
            server->PushReplicationConnection(guid);
        });
        server->RegisterRPC<Step>([&](const Step &step, Sender sender, World &handlerWorld) {
            if (const auto avatar = handlerWorld.Write<Avatar>(sender.Avatar())) {
                avatar->outOfOrder += step.sequence == avatar->lastStep + 1 ? 0 : 1;
                avatar->lastStep = step.sequence;
            }
            if (handled.fetch_add(1) == 0) {
                const std::lock_guard guard(mutex);
                handlerThreads.push_back(std::this_thread::get_id());
            }
        });

        Framework::Networking::PacketThread packets;
        packets.Start(*server);
        std::atomic<bool> stopTicking {false};
        std::atomic<int> ticks {0};
        std::thread tick([&] {
            while (!stopTicking) {
                server->Update();
                ++ticks;
                std::this_thread::sleep_for(16ms);
            }
        });

        std::vector<std::unique_ptr<Player>> players;
        bool connecting = true;
        for (int i = 0; i < kPlayers; ++i) {
            players.push_back(std::make_unique<Player>());
            connecting = connecting && static_cast<bool>(players.back()->net.Connect("127.0.0.1", port));
        }
        const auto update = [&] {
            for (auto &player : players) {
                player->net.Update();
            }
        };
        const auto read = [&] {
            std::vector<Seen> seen;
            for (auto &player : players) {
                const auto avatar = world.Read<Avatar>(replication->GetViewerId(player->Guid()));
                seen.push_back(avatar ? Seen {avatar->counter, avatar->position.x, avatar->lastStep, avatar->outOfOrder} : Seen {});
            }
            return seen;
        };
        const auto pumpUntil = [&](const auto &done, std::chrono::milliseconds limit) {
            const auto deadline = Clock::now() + limit;
            while (Clock::now() < deadline) {
                update();
                if (done()) {
                    return true;
                }
                std::this_thread::sleep_for(1ms);
            }
            return false;
        };
        // Every client has been sent its own avatar.
        const bool in = connecting && pumpUntil(
                                          [&] {
                                              for (auto &player : players) {
                                                  if (player->Own() == nullptr) {
                                                      return false;
                                                  }
                                              }
                                              return true;
                                          },
                                          15s);

        std::vector<Seen> before, after;
        bool caughtUp = false;
        if (in) {
            // Two seconds of every client writing its avatar and sending a step, each update. The server's guid is
            // asked for once here: RakPeer answers GetGUIDFromIndex from a table its own thread writes.
            const MafiaNet::RakNetGUID serverGuid = server->GetPeer()->GetMyGUID();
            before = read();
            pumpUntil(
                [&] {
                    for (auto &player : players) {
                        Avatar *own = player->Own();
                        ++own->counter;
                        own->position.x += 0.5f;
                        Step step;
                        step.sequence = ++player->sent;
                        player->net.SendRPC(step, serverGuid);
                    }
                    return false;
                },
                2000ms);
            // Then until the server has handled every step that was sent.
            caughtUp = pumpUntil(
                [&] {
                    const std::vector<Seen> now = read();
                    for (size_t i = 0; i < players.size(); ++i) {
                        if (now[i].lastStep != players[i]->sent) {
                            return false;
                        }
                    }
                    return true;
                },
                10s);
            after = read();
        }

        // A clean stop: the packet thread first, then the tick, then the clients and the peer.
        const auto stopping = Clock::now();
        packets.Stop();
        stopTicking = true;
        tick.join();
        players.clear();
        replication->Clear();
        server->Shutdown();
        const double stopMs = std::chrono::duration<double, std::milli>(Clock::now() - stopping).count();

        EQUALS(in, true);
        if (!in) {
            break;
        }
        EQUALS(caughtUp, true);
        uint32_t leastAdvance = ~0u;
        float leastMoved      = 1e9f;
        uint32_t outOfOrder   = 0;
        uint32_t steps        = 0;
        for (size_t i = 0; i < after.size(); ++i) {
            leastAdvance = std::min(leastAdvance, after[i].counter - before[i].counter);
            leastMoved   = std::min(leastMoved, after[i].x - before[i].x);
            outOfOrder += after[i].outOfOrder;
            steps += after[i].lastStep;
        }
        fprintf(stderr, "packet_drain: %d clients, 2 workers, %d ticks: counters advanced by at least %u, poses by %.1f; %u steps handled, %u out of order; stopped in %.0f ms\n", kPlayers, ticks.load(), leastAdvance, leastMoved, steps, outOfOrder,
            stopMs);
        GREATER(leastAdvance, 5u);
        GREATER(leastMoved, 2.0f);
        EQUALS(outOfOrder, 0u);
        GREATER(steps, static_cast<uint32_t>(kPlayers * 100));
        EQUALS(handled.load(), steps);
        // Handlers ran on the packet thread: not here, and not where the ticks ran.
        EQUALS(handlerThreads.size(), static_cast<size_t>(1));
        EQUALS(handlerThreads.empty() || handlerThreads[0] != std::this_thread::get_id(), true);
        // Not a pace: a hosted runner with three cores gets through a few dozen in the two seconds.
        GREATER(ticks.load(), 10);
        LESSER(stopMs, 5000.0);
    });
});
