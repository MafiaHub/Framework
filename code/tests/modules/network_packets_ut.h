/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_peer.h"
#include "networking/network_server.h"
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

    IT("dispatches voice frames without unknown-packet spam and logs unclaimed packets", {
        Framework::Networking::NetworkServer server;
        struct Shutdown {
            Framework::Networking::NetworkServer &server;
            ~Shutdown() {
                server.Shutdown();
            }
        } shutdown {server};
        // An ephemeral loopback socket lets Receive() drain injected packets without a client.
        MafiaNet::SocketDescriptor socket(0, "127.0.0.1");
        EQUALS(server.GetPeer()->Startup(1, &socket, 1), MafiaNet::RAKNET_STARTED);

        // The test runner pauses logging; capture its synchronous null logger without opening files.
        auto logger = Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING, false);
        auto logs = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(8);
        logs->set_pattern("%v");
        struct RestoreLogger {
            std::shared_ptr<spdlog::logger> logger;
            spdlog::level::level_enum level;
            ~RestoreLogger() {
                logger->sinks().pop_back();
                logger->set_level(level);
            }
        } restoreLogger {logger, logger->level()};
        logger->sinks().push_back(logs);
        logger->set_level(spdlog::level::trace);

        auto enqueue = [&](uint8_t id) {
            auto *packet = server.GetPeer()->AllocatePacket(1);
            packet->data[0] = id;
            server.GetPeer()->PushBackPacket(packet, false);
        };
        int voiceFrames = 0;
        int declinedPackets = 0;
        server.SetUnknownPacketHandler([&](MafiaNet::Packet *packet) {
            if (packet->data[server.GetPacketDataOffset()] == ID_RAKVOICE_RELAY_DATA) {
                ++voiceFrames;
                return true;
            }
            ++declinedPackets;
            return false;
        });

        for (int i = 0; i < 100; ++i) {
            enqueue(ID_RAKVOICE_RELAY_DATA);
        }
        server.Update();
        EQUALS(voiceFrames, 100);
        EQUALS(logs->last_raw().empty(), true);

        enqueue(ID_USER_PACKET_ENUM);
        server.Update();
        EQUALS(declinedPackets, 1);
        EQUALS(logs->last_raw().size(), static_cast<size_t>(1));
        EQUALS(logs->last_formatted()[0].find("Received unknown packet " + std::to_string(ID_USER_PACKET_ENUM)), static_cast<size_t>(0));

        // A known id still gets a diagnostic when no application handler claims it.
        server.SetUnknownPacketHandler({});
        enqueue(ID_RAKVOICE_RELAY_DATA);
        server.Update();
        EQUALS(logs->last_raw().size(), static_cast<size_t>(2));
        EQUALS(logs->last_formatted()[1].find("Received unknown packet " + std::to_string(ID_RAKVOICE_RELAY_DATA)), static_cast<size_t>(0));
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
        out.epicId     = "epic-4";
        out.ticket     = "one-time-ticket";

        MafiaNet::BitStream bs;
        out.Serialize(&bs, true);
        const RPC::ClientIdentity in = RPC::Read<RPC::ClientIdentity>(&bs);
        STREQUALS(in.name.c_str(), "kheartz");
        STREQUALS(in.steamId.c_str(), "steam-1");
        STREQUALS(in.discordId.c_str(), "discord-2");
        STREQUALS(in.hardwareId.c_str(), "hw-3");
        STREQUALS(in.epicId.c_str(), "epic-4");
        STREQUALS(in.ticket.c_str(), "one-time-ticket");
    });

    // The identity is the MafiaNet session payload: bytes the server decodes before any connection
    // exists, so what a client encodes must decode whole, and nothing else may.
    IT("decodes a ClientIdentity session payload it encoded", {
        RPC::ClientIdentity out {};
        out.name       = "Jan";
        out.steamId    = "76561198000000000";
        out.discordId  = "123";
        out.hardwareId = "456";
        out.epicId     = "abc123def456";
        out.ticket     = "tkt-abc+123";

        const auto in = RPC::ClientIdentity::Decode(out.Encode());
        EQUALS(in.has_value(), true);
        STREQUALS(in->name.c_str(), "Jan");
        STREQUALS(in->steamId.c_str(), "76561198000000000");
        STREQUALS(in->discordId.c_str(), "123");
        STREQUALS(in->hardwareId.c_str(), "456");
        STREQUALS(in->epicId.c_str(), "abc123def456");
        STREQUALS(in->ticket.c_str(), "tkt-abc+123");
    });

    IT("decodes an identity with every field empty", {
        RPC::ClientIdentity out {};
        const auto in = RPC::ClientIdentity::Decode(out.Encode());
        EQUALS(in.has_value(), true);
        EQUALS(in->name.empty() && in->ticket.empty(), true);
    });

    IT("refuses an empty session payload", {
        EQUALS(RPC::ClientIdentity::Decode("").has_value(), false);
    });

    IT("refuses a truncated session payload", {
        RPC::ClientIdentity out {};
        out.name   = "Jan";
        out.ticket = "a-ticket-long-enough-to-cut";
        const std::string encoded = out.Encode();
        EQUALS(RPC::ClientIdentity::Decode(encoded.substr(0, encoded.size() - 4)).has_value(), false);
    });

    IT("refuses a payload from something that is not a framework client", {
        EQUALS(RPC::ClientIdentity::Decode("{\"build\":\"m2o|1.2.3\"}").has_value(), false);
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
