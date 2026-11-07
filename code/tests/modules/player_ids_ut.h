/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "connection_admission_ut.h" // ConnectionAdmissionRig: a server and client over loopback

#include "networking/network_server.h"
#include "networking/player_ids.h"
#include "networking/replication/entity_registry.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"

#include <vector>

namespace PlayerIdsTest {
    struct LootSpot final: Framework::Networking::Replication::NetworkEntity {
        static constexpr const char *kTypeName = "Test::PlayerIds::LootSpot";
    };
} // namespace PlayerIdsTest

MODULE(player_ids, {
    using Framework::Networking::PlayerIdAllocator;

    IT("numbers players from 1 in join order", {
        PlayerIdAllocator ids;
        EQUALS(ids.Assign(0xA1), 1u);
        EQUALS(ids.Assign(0xB2), 2u);
        EQUALS(ids.Assign(0xC3), 3u);
        EQUALS(ids.Size(), size_t {3});
    });

    IT("gives a connection the same ID on every call", {
        PlayerIdAllocator ids;
        EQUALS(ids.Assign(0xA1), 1u);
        EQUALS(ids.Assign(0xA1), 1u);
        EQUALS(ids.Assign(0xB2), 2u);
        EQUALS(ids.Size(), size_t {2});
    });

    IT("never hands out a disconnected player's ID again", {
        PlayerIdAllocator ids;
        EQUALS(ids.Assign(0xA1), 1u);
        EQUALS(ids.Assign(0xB2), 2u);
        ids.Release(0xA1);
        ids.Release(0xB2);

        // The same connection guid coming back is a new session, so a new ID.
        EQUALS(ids.Assign(0xA1), 3u);
        EQUALS(ids.Assign(0xC3), 4u);
    });

    IT("resolves both ways while held, and neither way after release", {
        PlayerIdAllocator ids;
        ids.Assign(0xA1);
        const auto id = ids.Assign(0xB2);
        EQUALS(ids.Get(0xB2), id);
        EQUALS(ids.FindGuid(id).value_or(0), uint64_t {0xB2});

        ids.Release(0xB2);
        EQUALS(ids.Get(0xB2), PlayerIdAllocator::kInvalid);
        EQUALS(ids.FindGuid(id).has_value(), false);
        EQUALS(ids.Get(0xA1), 1u);
        EQUALS(ids.FindGuid(PlayerIdAllocator::kInvalid).has_value(), false);
        ids.Release(0xFFFF); // never held: a no-op
        EQUALS(ids.Size(), size_t {1});
    });

    IT("wraps to 1 at the end of the range, skipping IDs still held", {
        PlayerIdAllocator ids(4);
        EQUALS(ids.Assign(0x1), 1u);
        EQUALS(ids.Assign(0x2), 2u);
        EQUALS(ids.Assign(0x3), 3u);
        EQUALS(ids.Assign(0x4), 4u);
        ids.Release(0x2);
        ids.Release(0x4);

        // 1 and 3 are still in use, so the wrap lands on 2, then 4.
        EQUALS(ids.Assign(0x5), 2u);
        EQUALS(ids.Assign(0x6), 4u);
    });

    IT("refuses an ID only when every one is held", {
        PlayerIdAllocator ids(2);
        EQUALS(ids.Assign(0x1), 1u);
        EQUALS(ids.Assign(0x2), 2u);
        EQUALS(ids.Assign(0x3), PlayerIdAllocator::kInvalid);
        EQUALS(ids.Get(0x3), PlayerIdAllocator::kInvalid);

        ids.Release(0x1);
        EQUALS(ids.Assign(0x3), 1u);
    });

    IT("starts at 1 however many entities the server created first", {
        using namespace Framework::Networking::Replication;
        using PlayerIdsTest::LootSpot;

        Framework::Networking::NetworkServer server;
        ReplicationManager *manager = server.GetReplicationManager();
        manager->Init(&server, true);
        struct Cleanup {
            ReplicationManager *manager;
            ~Cleanup() {
                manager->Clear();
            }
        } cleanup {manager};
        EntityRegistry::Get().Register<LootSpot>(LootSpot::kTypeName);

        // The reported case: thousands of server-authoritative loot spots spawned at startup.
        std::vector<LootSpot *> spots;
        for (int i = 0; i < 4005; ++i) {
            spots.push_back(manager->CreateEntity<LootSpot>());
        }
        NEQUALS(spots.back(), nullptr);
        GREATER(spots.back()->GetNetworkID(), uint64_t {4004});

        const MafiaNet::RakNetGUID first(0x1001);
        const MafiaNet::RakNetGUID second(0x1002);
        EQUALS(server.GetPlayerId(first), 0u);
        EQUALS(server.AssignPlayerId(first), 1u);
        EQUALS(server.AssignPlayerId(second), 2u);
        EQUALS(server.GetPlayerId(first), 1u);

        const auto found = server.FindPlayerById(2);
        EQUALS(found.has_value(), true);
        EQUALS(static_cast<uint64_t>(*found), uint64_t {0x1002});
        EQUALS(server.FindPlayerById(3).has_value(), false);

        for (auto *spot : spots) {
            manager->DestroyEntity(spot);
        }
    });

    // The disconnect handlers (the game's OnPlayerDisconnect, the scripts' playerDisconnect) run
    // when the server handles the close. The player's ID and identity must still resolve then, on
    // every way a connection ends, and be gone once it is handled.
    // Declared before the rig so it outlives the server callbacks that write to it.
    struct JoinedPlayer {
        MafiaNet::RakNetGUID guid;
        bool connected       = false;
        bool handled         = false;
        uint32_t idInHandler = 0;
        std::string nameInHandler;

        // Connects the rig's client and gives it an identity and a player ID, as a join does.
        bool Join(ConnectionAdmissionRig &rig) {
            rig.server->SetOnPlayerConnectCallback([this](MafiaNet::Packet *packet) {
                guid      = packet->guid;
                connected = true;
            });
            if (!rig.Connect("") || !rig.PumpUntil([&] { return connected && rig.seen.clientConnected && rig.server->IsAuthenticated(guid); })) {
                return false;
            }
            Framework::Networking::RPC::ClientIdentity identity;
            identity.name = "Jan";
            rig.server->SetPeerIdentity(guid, identity);
            rig.server->AssignPlayerId(guid);
            auto *server = rig.server.get();
            server->SetOnPlayerDisconnectCallback([this, server](MafiaNet::Packet *, Framework::Networking::DisconnectionReason, const std::string &) {
                handled             = true;
                idInHandler         = server->GetPlayerId(guid);
                const auto *known   = server->GetPeerIdentity(guid);
                nameInHandler       = known ? known->name : "";
            });
            return true;
        }
    };

    IT("keeps a kicked player's ID and identity until the server handles the disconnect", {
        JoinedPlayer player;
        ConnectionAdmissionRig rig;
        EQUALS(rig.StartServer(false), true);
        EQUALS(player.Join(rig), true);
        EQUALS(rig.server->GetPlayerId(player.guid), 1u);

        rig.server->KickPlayer(player.guid, Framework::Networking::DisconnectionReason::KICKED_CUSTOM, "bye");
        // Closing, not closed: the kick shuts the build gate and releases nothing else.
        EQUALS(rig.server->IsAuthenticated(player.guid), false);
        EQUALS(rig.server->GetPlayerId(player.guid), 1u);

        EQUALS(rig.PumpUntil([&] { return player.handled; }), true);
        EQUALS(player.idInHandler, 1u);
        STREQUALS(player.nameInHandler.c_str(), "Jan");
        EQUALS(rig.server->GetPlayerId(player.guid), 0u);
        EQUALS(rig.server->GetPeerIdentity(player.guid) == nullptr, true);
        EQUALS(rig.server->FindPlayerById(1).has_value(), false);
    });

    IT("keeps a leaving player's ID and identity until the server handles the disconnect", {
        JoinedPlayer player;
        ConnectionAdmissionRig rig;
        EQUALS(rig.StartServer(false), true);
        EQUALS(player.Join(rig), true);

        EQUALS(static_cast<bool>(rig.client->Disconnect()), true);

        EQUALS(rig.PumpUntil([&] { return player.handled; }), true);
        EQUALS(player.idInHandler, 1u);
        STREQUALS(player.nameInHandler.c_str(), "Jan");
        EQUALS(rig.server->GetPlayerId(player.guid), 0u);
        EQUALS(rig.server->GetPeerIdentity(player.guid) == nullptr, true);
    });
})
