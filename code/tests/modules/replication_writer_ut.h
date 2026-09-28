/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_server.h"
#include "networking/replication/replication_manager.h"
#include "networking/replication/replication_writer.h"

#include <array>
#include <vector>

MODULE(replication_writer, {
    using Framework::Networking::Replication::ReplicationWriter;
    struct Packet {
        std::vector<unsigned char> data;
        MafiaNet::BitSize_t bits;
        MafiaNet::PRO pro;
    };
    struct Harness {
        ReplicationWriter writer;
        MafiaNet::BitStream channels[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS];
        MafiaNet::PRO parameters[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS];
        bool selected[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS] {};
        std::vector<Packet> packets;
        ReplicationWriter::SendPacket send = [this](MafiaNet::BitStream &bs, const MafiaNet::PRO &pro) {
            packets.push_back({std::vector<unsigned char>(bs.GetData(), bs.GetData() + bs.GetNumberOfBytesUsed()), bs.GetNumberOfBitsUsed(), pro});
        };

        Harness() {
            for (auto &pro : parameters) {
                pro.priority        = MafiaNet::Priority::High;
                pro.reliability     = MafiaNet::Reliability::ReliableOrdered;
                pro.orderingChannel = 3;
                pro.sendReceipt     = 0;
            }
            parameters[0].reliability     = MafiaNet::Reliability::Unreliable;
            parameters[0].orderingChannel = 0;
            parameters[1].orderingChannel = 1;
            // A non-byte-aligned pose, and stale cached state: exactly the
            // shape that previously generated empty reliable messages.
            channels[0].Write(static_cast<std::uint32_t>(123));
            channels[0].Write(true);
            channels[1].Write(static_cast<std::uint32_t>(456));
        }
        bool Write(MafiaNet::NetworkID id, bool batched = false, MafiaNet::Time time = 100, std::size_t budget = 1100, MafiaNet::WorldId world = 0) {
            return writer.Write(id, selected, channels, time, parameters, world, batched, budget, send);
        }
        void Flush() {
            writer.Flush(send);
        }
    };

    IT("omits stale cached state and never emits empty channel groups", {
        Harness h;
        h.selected[0] = true;
        EQUALS(h.Write(1), true);
        EQUALS(h.packets.size(), 1u);
        EQUALS(h.packets[0].pro.reliability, MafiaNet::Reliability::Unreliable);
        h.packets.clear();
        h.selected[0] = false;
        h.selected[1] = true;
        EQUALS(h.Write(1), true);
        EQUALS(h.packets.size(), 1u);
        EQUALS(h.packets[0].pro.reliability, MafiaNet::Reliability::ReliableOrdered);
        h.packets.clear();
        h.selected[1] = false;
        EQUALS(h.Write(1), false);
        h.Flush();
        EQUALS(h.packets.size(), 0u);
    });

    IT("preserves RM3 framing and selected bits without consuming cached streams", {
        Harness h;
        h.selected[0] = h.selected[1] = true;
        h.channels[0].IgnoreBits(7);
        h.Write(91);
        EQUALS(h.packets.size(), 2u);
        EQUALS(h.channels[0].GetReadOffset(), 7u);
        for (std::size_t message = 0; message < h.packets.size(); ++message) {
            auto &packet = h.packets[message];
            MafiaNet::BitStream bs(packet.data.data(), static_cast<unsigned int>(packet.data.size()), false);
            MafiaNet::MessageID id;
            MafiaNet::Time timestamp;
            MafiaNet::WorldId world;
            MafiaNet::NetworkID entity;
            bs.Read(id);
            EQUALS(id, ID_TIMESTAMP);
            bs.Read(timestamp);
            EQUALS(timestamp, 100u);
            bs.Read(id);
            EQUALS(id, ID_REPLICA_MANAGER_SERIALIZE);
            bs.Read(world);
            EQUALS(world, 0u);
            bs.Read(entity);
            EQUALS(entity, 91u);
            for (int channel = 0; channel < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++channel) {
                bool present = false;
                EQUALS(bs.Read(present), true);
                EQUALS(present, (channel == static_cast<int>(message)));
                if (present) {
                    MafiaNet::BitSize_t bits;
                    bs.ReadCompressed(bits);
                    EQUALS(bits, h.channels[channel].GetNumberOfBitsUsed());
                    bs.AlignReadToByteBoundary();
                    MafiaNet::BitStream value;
                    EQUALS(bs.Read(value, bits), true);
                    std::uint32_t number;
                    value.Read(number);
                    EQUALS(number, (channel == 0 ? 123u : 456u));
                    if (channel == 0) {
                        bool flag = false;
                        value.Read(flag);
                        EQUALS(flag, true);
                    }
                }
            }
        }
    });

    IT("batches exact pose bits and flushes within the payload budget", {
        Harness h;
        h.selected[0] = true;
        for (MafiaNet::NetworkID id = 1; id <= 100; ++id) {
            h.Write(id, true, 100, 100);
        }
        h.Flush();
        LESSER(h.packets.size(), 100u);
        unsigned int total = 0;
        for (auto &packet : h.packets) {
            EQUALS((packet.data.size() <= 100), true);
            EQUALS(packet.pro.reliability, MafiaNet::Reliability::Unreliable);
            MafiaNet::BitStream bs(packet.data.data(), static_cast<unsigned int>(packet.data.size()), false);
            MafiaNet::Time timestamp;
            MafiaNet::WorldId world;
            ReplicationWriter::TransformEntries entries;
            EQUALS(ReplicationWriter::ReadBatch(bs, timestamp, world, entries), true);
            EQUALS(timestamp, 100u);
            EQUALS(world, 0u);
            for (const auto &entry : entries) {
                ++total;
                EQUALS(entry.networkId, total);
                EQUALS(entry.bits, 33u);
                MafiaNet::BitStream pose(bs.GetData() + entry.offset / 8, 5, false);
                std::uint32_t number;
                bool flag = false;
                pose.Read(number);
                pose.Read(flag);
                EQUALS(number, 123u);
                EQUALS(flag, true);
            }
        }
        EQUALS(total, 100u);
    });

    IT("keeps state immediate and isolates batch timestamps worlds and recipients", {
        Harness a, b;
        a.selected[0] = a.selected[1] = b.selected[0] = true;
        a.Write(1, true);
        EQUALS(a.packets.size(), 1u);
        EQUALS(a.packets[0].pro.reliability, MafiaNet::Reliability::ReliableOrdered);
        a.selected[1] = false;
        a.Write(2, true, 101);
        EQUALS(a.packets.size(), 2u);
        a.Write(3, true, 101, 1100, 1);
        EQUALS(a.packets.size(), 3u);
        b.Write(4, true);
        EQUALS(b.packets.size(), 0u);
        a.Flush();
        b.Flush();
        EQUALS(a.packets.size(), 4u);
        EQUALS(b.packets.size(), 1u);
    });

    IT("falls back to RM3 for transforms exceeding the batch budget", {
        Harness h;
        h.selected[0] = true;
        h.Write(1, true, 100, 20);
        h.Flush();
        EQUALS(h.packets.size(), 1u);
        EQUALS(h.packets[0].data[1 + sizeof(MafiaNet::Time)], ID_REPLICA_MANAGER_SERIALIZE);
    });

    IT("encodes small IDs compactly and preserves full-width IDs", {
        Harness h;
        h.selected[0] = true;
        const MafiaNet::NetworkID ids[] {1, 127, 128, 16384, 0x123456789ABCDEF0ULL};
        for (auto id : ids) {
            h.Write(id, true);
        }
        h.Flush();
        EQUALS(h.packets.size(), 1u);
        auto &data = h.packets[0].data;
        // Shared header is 13 bytes, then ID=1 in one byte and a uint16 length.
        EQUALS(data[13], 1u);
        MafiaNet::BitStream bs(data.data(), static_cast<unsigned int>(data.size()), false);
        MafiaNet::Time timestamp;
        MafiaNet::WorldId world;
        ReplicationWriter::TransformEntries entries;
        EQUALS(ReplicationWriter::ReadBatch(bs, timestamp, world, entries), true);
        EQUALS(entries.size(), 5u);
        for (std::size_t i = 0; i < entries.size(); ++i) {
            EQUALS(entries[i].networkId, ids[i]);
        }
        EQUALS(entries[0].offset, 16u * 8);
        // Unterminated/overflowing 10-byte varint at the first entry.
        std::fill(data.begin() + 13, data.begin() + 23, 0xFF);
        MafiaNet::BitStream malformed(data.data(), static_cast<unsigned int>(data.size()), false);
        EQUALS(ReplicationWriter::ReadBatch(malformed, timestamp, world, entries), false);
        EQUALS(entries.size(), 0u);
    });

    IT("rejects truncated batches and extra bytes before applying any entries", {
        Harness h;
        h.selected[0] = true;
        h.Write(1, true);
        h.Write(2, true);
        h.Flush();
        auto &data = h.packets[0].data;
        MafiaNet::Time timestamp;
        MafiaNet::WorldId world;
        ReplicationWriter::TransformEntries entries;
        for (unsigned int size = 0; size < data.size(); ++size) {
            MafiaNet::BitStream bs(data.data(), size, false);
            EQUALS(ReplicationWriter::ReadBatch(bs, timestamp, world, entries), false);
        }
        data.push_back(0);
        MafiaNet::BitStream bs(data.data(), static_cast<unsigned int>(data.size()), false);
        EQUALS(ReplicationWriter::ReadBatch(bs, timestamp, world, entries), false);
    });

    IT("applies batches through ownership epoch and per-entity timestamp gates", {
        // Unstarted peer: dispatch real packets through the manager without
        // binding sockets or connecting any stress clients.
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
            h.channels[0].Reset();
            h.channels[0].Write(epoch);
            Framework::Networking::Replication::NetworkEntity source;
            source.position.x = x;
            Framework::Networking::Replication::FieldSerializer fields(&h.channels[0], true);
            source.SerializeTransform(fields);
            h.selected[0] = true;
            h.Write(9999, true, time); // Unknown entity must not block the following one.
            h.Write(7, true, time);
            h.Flush();
            auto &bytes = h.packets[0].data;
            MafiaNet::Packet packet {};
            packet.data   = bytes.data();
            packet.length = static_cast<unsigned int>(bytes.size()) - (truncate ? 1 : 0);
            packet.guid   = sender;
            manager->OnReceive(&packet);
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

    IT("resets reusable pose storage between entities and packets", {
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
            h.selected[0] = true;
            for (unsigned int i = 0; i < 2; ++i) {
                h.channels[0].Reset();
                h.channels[0].Write(static_cast<uint8_t>(i + 1));
                Framework::Networking::Replication::NetworkEntity source;
                source.position.x = static_cast<float>(10 * pass + i);
                Framework::Networking::Replication::FieldSerializer fields(&h.channels[0], true);
                source.SerializeTransform(fields);
                h.Write(i + 1, true, 100 + pass);
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
        manager->Update(); // Explicit standalone calls remain supported.
        EQUALS(entity.passes, 3);
    });
});
