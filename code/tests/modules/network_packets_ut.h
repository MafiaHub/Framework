/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_peer.h"
#include "networking/rpc/chat_message.h"
#include "networking/rpc/client_identity.h"
#include "networking/rpc/server_resources.h"

#include <mafianet/BitStream.h>

#include <cstdint>
#include <vector>

// Networking wire-format tests: the ID_TIMESTAMP packet-id offset (a wrong skip width once read the
// id mid-timestamp and faked an ID_CONNECTION_LOST), plus serialize/deserialize round-trips of the
// RPC payloads so a write/read asymmetry can't slip through.
MODULE(network_packets, {
    using Framework::Networking::NetworkPeer;
    namespace RPC = Framework::Networking::RPC;

    IT("resolves the packet id after a full ID_TIMESTAMP + Time prefix", {
        std::vector<uint8_t> buf;
        buf.push_back(ID_TIMESTAMP);
        for (size_t i = 0; i < sizeof(MafiaNet::Time); i++) {
            buf.push_back(static_cast<uint8_t>(0x80 + i)); // arbitrary timestamp bytes
        }
        buf.push_back(static_cast<uint8_t>(ID_REPLICA_MANAGER_SERIALIZE));
        buf.push_back(0x42);

        const int offset = NetworkPeer::ResolvePacketDataOffset(buf.data(), static_cast<uint32_t>(buf.size()));
        EQUALS(offset, 1 + static_cast<int>(sizeof(MafiaNet::Time)));
        // The decisive check: the byte at the offset is the real id, not a timestamp byte.
        EQUALS(buf[static_cast<size_t>(offset)], static_cast<uint8_t>(ID_REPLICA_MANAGER_SERIALIZE));
    });

    IT("returns offset 0 for a packet without a timestamp", {
        std::vector<uint8_t> buf = {static_cast<uint8_t>(ID_CONNECTION_REQUEST_ACCEPTED), 0x01, 0x02};
        EQUALS(NetworkPeer::ResolvePacketDataOffset(buf.data(), static_cast<uint32_t>(buf.size())), 0);
    });

    IT("rejects a truncated ID_TIMESTAMP frame", {
        // Starts with ID_TIMESTAMP but can't hold the 8-byte Time plus a real id: malformed -> -1.
        std::vector<uint8_t> buf = {static_cast<uint8_t>(ID_TIMESTAMP), 0x00, 0x01};
        EQUALS(NetworkPeer::ResolvePacketDataOffset(buf.data(), static_cast<uint32_t>(buf.size())), -1);
    });

    IT("rejects an empty datagram", {
        EQUALS(NetworkPeer::ResolvePacketDataOffset(nullptr, 0), -1);
    });

    IT("classifies all ReplicaManager3 ids as replication packets", {
        EQUALS(NetworkPeer::IsReplicationPacket(ID_REPLICA_MANAGER_CONSTRUCTION), true);
        EQUALS(NetworkPeer::IsReplicationPacket(ID_REPLICA_MANAGER_SCOPE_CHANGE), true);
        EQUALS(NetworkPeer::IsReplicationPacket(ID_REPLICA_MANAGER_SERIALIZE), true);
        EQUALS(NetworkPeer::IsReplicationPacket(ID_REPLICA_MANAGER_DOWNLOAD_STARTED), true);
        EQUALS(NetworkPeer::IsReplicationPacket(ID_REPLICA_MANAGER_DOWNLOAD_COMPLETE), true);
        EQUALS(NetworkPeer::IsReplicationPacket(ID_USER_PACKET_ENUM), false);
        EQUALS(NetworkPeer::IsReplicationPacket(ID_CONNECTION_LOST), false);
    });

    IT("round-trips a ChatMessage payload", {
        RPC::ChatMessage out {};
        out.text = "Expecto Patronum!";

        MafiaNet::BitStream bs;
        out.Serialize(&bs, true);
        const RPC::ChatMessage in = RPC::Read<RPC::ChatMessage>(&bs);
        STREQUALS(in.text.c_str(), "Expecto Patronum!");
    });

    IT("round-trips a ClientIdentity payload", {
        RPC::ClientIdentity out {};
        out.name       = "kheartz";
        out.steamId    = "steam-1";
        out.discordId  = "discord-2";
        out.hardwareId = "hw-3";

        MafiaNet::BitStream bs;
        out.Serialize(&bs, true);
        const RPC::ClientIdentity in = RPC::Read<RPC::ClientIdentity>(&bs);
        STREQUALS(in.name.c_str(), "kheartz");
        STREQUALS(in.steamId.c_str(), "steam-1");
        STREQUALS(in.discordId.c_str(), "discord-2");
        STREQUALS(in.hardwareId.c_str(), "hw-3");
    });

    IT("round-trips a ServerResources payload with a resource list", {
        RPC::ServerResources out {};
        out.readyEventId = 7;
        out.tickRate     = 0.0166f;
        out.resources.push_back({"gamemode", "1.0.0"});
        out.resources.push_back({"wizard-test", "0.0.1"});

        MafiaNet::BitStream bs;
        out.Serialize(&bs, true);
        const RPC::ServerResources in = RPC::Read<RPC::ServerResources>(&bs);
        EQUALS(in.readyEventId, 7);
        EQUALS(in.tickRate, 0.0166f);
        EQUALS(in.resources.size(), static_cast<size_t>(2));
        STREQUALS(in.resources[0].name.c_str(), "gamemode");
        STREQUALS(in.resources[0].version.c_str(), "1.0.0");
        STREQUALS(in.resources[1].name.c_str(), "wizard-test");
        STREQUALS(in.resources[1].version.c_str(), "0.0.1");
    });
});
