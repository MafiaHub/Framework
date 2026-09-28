/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_server.h"
#include "networking/replication/replication_manager.h"
#include "networking/replication/transform_batch.h"

#include <vector>

MODULE(transform_batch, {
    struct Packet {
        std::vector<unsigned char> data;
        MafiaNet::PRO pro;
    };

    // One connection's batch, with the packets it would have handed to RakPeer.
    struct Harness {
        Framework::Networking::Replication::TransformBatch batch;
        MafiaNet::BitStream pose;
        MafiaNet::PRO parameters {};
        std::vector<Packet> packets;
        Framework::Networking::Replication::TransformBatch::SendPacket send = [this](MafiaNet::BitStream &bs, const MafiaNet::PRO &pro) {
            packets.push_back({std::vector<unsigned char>(bs.GetData(), bs.GetData() + bs.GetNumberOfBytesUsed()), pro});
        };

        Harness() {
            parameters.priority    = MafiaNet::Priority::High;
            parameters.reliability = MafiaNet::Reliability::Unreliable;
            // A pose that does not end on a byte boundary.
            pose.Write(static_cast<std::uint32_t>(123));
            pose.Write(true);
        }
        bool Queue(MafiaNet::NetworkID id, MafiaNet::Time time = 100, std::size_t budget = 1100, MafiaNet::WorldId world = 0) {
            return batch.Queue(id, pose, time, parameters, world, budget, send);
        }
        void Flush() {
            batch.Flush(send);
        }
    };

    auto read = [](Packet &packet, MafiaNet::Time &timestamp, MafiaNet::WorldId &world, Framework::Networking::Replication::TransformBatch::Entries &entries, std::size_t size) {
        MafiaNet::BitStream bs(packet.data.data(), static_cast<unsigned int>(size), false);
        return Framework::Networking::Replication::TransformBatch::Read(bs, timestamp, world, entries);
    };

    IT("batches exact pose bits and flushes within the payload budget", {
        Harness h;
        for (MafiaNet::NetworkID id = 1; id <= 100; ++id) {
            EQUALS(h.Queue(id, 100, 100), true);
        }
        h.Flush();
        LESSER(h.packets.size(), 100u);
        unsigned int total = 0;
        for (auto &packet : h.packets) {
            EQUALS((packet.data.size() <= 100), true);
            EQUALS(packet.pro.reliability, MafiaNet::Reliability::Unreliable);
            MafiaNet::Time timestamp;
            MafiaNet::WorldId world;
            Framework::Networking::Replication::TransformBatch::Entries entries;
            MafiaNet::BitStream bs(packet.data.data(), static_cast<unsigned int>(packet.data.size()), false);
            EQUALS(Framework::Networking::Replication::TransformBatch::Read(bs, timestamp, world, entries), true);
            EQUALS(timestamp, 100u);
            EQUALS(world, 0u);
            for (const auto &entry : entries) {
                ++total;
                EQUALS(entry.networkId, total);
                EQUALS(entry.bits, 33u);
                MafiaNet::BitStream value(packet.data.data() + entry.offset / 8, 5, false);
                std::uint32_t number;
                bool flag = false;
                value.Read(number);
                value.Read(flag);
                EQUALS(number, 123u);
                EQUALS(flag, true);
            }
        }
        EQUALS(total, 100u);
    });

    IT("starts a new batch per timestamp and world, and keeps each connection's batch its own", {
        Harness a, b;
        a.Queue(1);
        a.Queue(2, 101);
        EQUALS(a.packets.size(), 1u);
        a.Queue(3, 101, 1100, 1);
        EQUALS(a.packets.size(), 2u);
        b.Queue(4);
        EQUALS(b.packets.size(), 0u);
        a.Flush();
        b.Flush();
        EQUALS(a.packets.size(), 3u);
        EQUALS(b.packets.size(), 1u);
        b.Flush();
        EQUALS(b.packets.size(), 1u);
    });

    IT("refuses poses the format cannot carry, queueing nothing", {
        Harness h;
        EQUALS(h.Queue(1, 100, 20), false);
        EQUALS(h.Queue(1, 0), false);
        h.parameters.reliability = MafiaNet::Reliability::ReliableOrdered;
        EQUALS(h.Queue(1), false);
        h.parameters.reliability = MafiaNet::Reliability::Unreliable;
        h.pose.Reset();
        EQUALS(h.Queue(1), false);
        h.Flush();
        EQUALS(h.packets.size(), 0u);
    });

    IT("encodes small IDs compactly and preserves full-width IDs", {
        Harness h;
        const MafiaNet::NetworkID ids[] {1, 127, 128, 16384, 0x123456789ABCDEF0ULL};
        for (auto id : ids) {
            h.Queue(id);
        }
        h.Flush();
        EQUALS(h.packets.size(), 1u);
        auto &data = h.packets[0].data;
        // The shared header is 13 bytes, then ID 1 in one byte and a uint16 length.
        EQUALS(data[13], 1u);
        MafiaNet::Time timestamp;
        MafiaNet::WorldId world;
        Framework::Networking::Replication::TransformBatch::Entries entries;
        EQUALS(read(h.packets[0], timestamp, world, entries, data.size()), true);
        EQUALS(entries.size(), 5u);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            EQUALS(entries[i].networkId, ids[i]);
        }
        EQUALS(entries[0].offset, 16u * 8);
        // An unterminated, overflowing ten-byte varint at the first entry.
        std::fill(data.begin() + 13, data.begin() + 23, 0xFF);
        EQUALS(read(h.packets[0], timestamp, world, entries, data.size()), false);
        EQUALS(entries.size(), 0u);
    });

    IT("rejects truncated batches and extra bytes before returning any entry", {
        Harness h;
        h.Queue(1);
        h.Queue(2);
        h.Flush();
        auto &data = h.packets[0].data;
        MafiaNet::Time timestamp;
        MafiaNet::WorldId world;
        Framework::Networking::Replication::TransformBatch::Entries entries;
        for (std::size_t size = 0; size < data.size(); ++size) {
            EQUALS(read(h.packets[0], timestamp, world, entries, size), false);
        }
        data.push_back(0);
        EQUALS(read(h.packets[0], timestamp, world, entries, data.size()), false);
    });

    IT("recognises only timestamped batches as batches", {
        Harness h;
        h.Queue(1);
        h.Flush();
        const auto &data = h.packets[0].data;
        EQUALS(Framework::Networking::Replication::TransformBatch::IsBatch(data.data(), static_cast<unsigned int>(data.size())), true);
        // A game's own raw message that takes the reserved identifier without a timestamp.
        const unsigned char game[] {Framework::Networking::Replication::TransformBatch::kId, 1, 2, 3};
        EQUALS(Framework::Networking::Replication::TransformBatch::IsBatch(game, sizeof(game)), false);
        EQUALS(Framework::Networking::Replication::TransformBatch::IsBatch(data.data(), 9), false);
    });

    IT("passes packets that are not batches on to their own handler", {
        Framework::Networking::NetworkServer peer;
        auto *manager = peer.GetReplicationManager();
        manager->Init(&peer, true);
        unsigned char game[] {Framework::Networking::Replication::TransformBatch::kId, 1, 2, 3};
        MafiaNet::Packet packet {};
        packet.data   = game;
        packet.length = sizeof(game);
        packet.guid   = MafiaNet::RakNetGUID(1001);
        EQUALS(manager->OnReceive(&packet), MafiaNet::RR_CONTINUE_PROCESSING);
    });

    IT("applies batches through ownership epoch and per-entity timestamp gates", {
        // An unstarted peer: real packets go through the manager without binding a socket.
        Framework::Networking::NetworkServer peer;
        auto *manager = peer.GetReplicationManager();
        manager->Init(&peer, true);
        struct Cleanup {
            Framework::Networking::Replication::ReplicationManager *manager;
            ~Cleanup() {
                manager->Clear();
            }
        } cleanup {manager};
        const MafiaNet::RakNetGUID owner(1001), stranger(1002);
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, owner));
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, stranger));
        Framework::Networking::Replication::NetworkEntity entity;
        entity.SetNetworkID(7);
        entity.ownerGUID  = MafiaNet::ToPeerGuid(owner);
        entity.stateEpoch = 3;
        manager->Reference(&entity);

        auto deliver = [&](MafiaNet::RakNetGUID sender, MafiaNet::Time time, std::uint8_t epoch, float x, bool truncate = false) {
            Harness h;
            h.pose.Reset();
            h.pose.Write(epoch);
            Framework::Networking::Replication::NetworkEntity source;
            source.position.x = x;
            Framework::Networking::Replication::FieldSerializer fields(&h.pose, true);
            source.SerializeTransform(fields);
            h.Queue(9999, time); // An unknown entity must not block the one after it.
            h.Queue(7, time);
            h.Flush();
            auto &bytes = h.packets[0].data;
            MafiaNet::Packet packet {};
            packet.data   = bytes.data();
            packet.length = static_cast<unsigned int>(bytes.size()) - (truncate ? 1 : 0);
            packet.guid   = sender;
            EQUALS(manager->OnReceive(&packet), MafiaNet::RR_STOP_PROCESSING_AND_DEALLOCATE);
        };
        deliver(stranger, 100, 3, 9.0f);
        EQUALS(entity.position.x, 0.0f);
        deliver(owner, 100, 2, 9.0f);
        EQUALS(entity.position.x, 0.0f);
        deliver(owner, 100, 3, 9.0f, true);
        EQUALS(entity.position.x, 0.0f);
        deliver(owner, 100, 3, 9.0f);
        EQUALS(entity.position.x, 9.0f);
        deliver(owner, 99, 3, 2.0f);
        EQUALS(entity.position.x, 9.0f);
        deliver(owner, 101, 3, 10.0f);
        EQUALS(entity.position.x, 10.0f);
        // The stack replica detaches before the peer tears down its connections.
    });

    IT("resets the reused pose stream between entities and packets", {
        Framework::Networking::NetworkServer peer;
        auto *manager = peer.GetReplicationManager();
        manager->Init(&peer, true);
        struct Cleanup {
            Framework::Networking::Replication::ReplicationManager *manager;
            ~Cleanup() {
                manager->Clear();
            }
        } cleanup {manager};
        const MafiaNet::RakNetGUID owner(1001);
        manager->PushConnection(manager->AllocConnection(MafiaNet::UNASSIGNED_SYSTEM_ADDRESS, owner));
        Framework::Networking::Replication::NetworkEntity entities[2];
        for (unsigned int i = 0; i < 2; ++i) {
            entities[i].SetNetworkID(i + 1);
            entities[i].ownerGUID  = MafiaNet::ToPeerGuid(owner);
            entities[i].stateEpoch = static_cast<uint8_t>(i + 1);
            manager->Reference(&entities[i]);
        }
        for (unsigned int pass = 1; pass <= 3; ++pass) {
            Harness h;
            for (unsigned int i = 0; i < 2; ++i) {
                h.pose.Reset();
                h.pose.Write(static_cast<uint8_t>(i + 1));
                Framework::Networking::Replication::NetworkEntity source;
                source.position.x = static_cast<float>(10 * pass + i);
                Framework::Networking::Replication::FieldSerializer fields(&h.pose, true);
                source.SerializeTransform(fields);
                h.Queue(i + 1, 100 + pass);
            }
            h.Flush();
            EQUALS(h.packets.size(), 1u);
            MafiaNet::Packet packet {};
            packet.data   = h.packets[0].data.data();
            packet.length = static_cast<unsigned int>(h.packets[0].data.size());
            packet.guid   = owner;
            manager->OnReceive(&packet);
            EQUALS(entities[0].position.x, static_cast<float>(10 * pass));
            EQUALS(entities[1].position.x, static_cast<float>(10 * pass + 1));
        }
    });

    IT("runs one replication world pass while draining many received packets", {
        Framework::Networking::NetworkServer peer;
        auto *manager = peer.GetReplicationManager();
        manager->Init(&peer, true);
        manager->SetAutoSerializeInterval(0);
        struct Counter: MafiaNet::Replica3 {
            void WriteAllocationID(MafiaNet::Connection_RM3 *, MafiaNet::BitStream *) const override {}
            MafiaNet::RM3ConstructionState QueryConstruction(MafiaNet::Connection_RM3 *, MafiaNet::ReplicaManager3 *) override {
                return MafiaNet::RM3CS_NO_ACTION;
            }
            bool QueryRemoteConstruction(MafiaNet::Connection_RM3 *) override {
                return false;
            }
            void SerializeConstruction(MafiaNet::BitStream *, MafiaNet::Connection_RM3 *) override {}
            bool DeserializeConstruction(MafiaNet::BitStream *, MafiaNet::Connection_RM3 *) override {
                return false;
            }
            void SerializeDestruction(MafiaNet::BitStream *, MafiaNet::Connection_RM3 *) override {}
            bool DeserializeDestruction(MafiaNet::BitStream *, MafiaNet::Connection_RM3 *) override {
                return false;
            }
            MafiaNet::RM3ActionOnPopConnection QueryActionOnPopConnection(MafiaNet::Connection_RM3 *) const override {
                return MafiaNet::RM3AOPC_DO_NOTHING;
            }
            void DeallocReplica(MafiaNet::Connection_RM3 *) override {}
            MafiaNet::RM3QuerySerializationResult QuerySerialization(MafiaNet::Connection_RM3 *) override {
                return MafiaNet::RM3QSR_DO_NOT_CALL_SERIALIZE;
            }
            MafiaNet::RM3SerializationResult Serialize(MafiaNet::SerializeParameters *) override {
                return MafiaNet::RM3SR_DO_NOT_SERIALIZE;
            }
            void Deserialize(MafiaNet::DeserializeParameters *) override {}
            int passes = 0;
            void OnUserReplicaPreSerializeTick() override {
                ++passes;
            }
        } entity;
        manager->Reference(&entity);
        manager->BeginNetworkUpdate();
        for (int packet = 0; packet < 300; ++packet) {
            manager->Update();
        }
        manager->EndNetworkUpdate();
        EQUALS(entity.passes, 1);
        manager->BeginNetworkUpdate();
        manager->Update();
        manager->EndNetworkUpdate();
        EQUALS(entity.passes, 2);
        manager->Update(); // Outside a drain every call runs a pass, as in ReplicaManager3.
        EQUALS(entity.passes, 3);
    });
});
