/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <function2/function2.hpp>
#include <mafianet/MessageIdentifiers.h>
#include <mafianet/ReplicaManager3.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Framework::Networking::Replication {
    // One writer per connection. State keeps the RM3 wire format; poses can share
    // an unreliable message. No cached, unselected channel ever reaches the wire.
    class ReplicationWriter final {
      public:
        // Reserved by Framework; game packet identifiers must start after this.
        static constexpr MafiaNet::MessageID kTransformBatchId = ID_USER_PACKET_ENUM;
        static constexpr std::size_t kMaxBatchBytes            = 1100;
        using SendPacket                                       = fu2::function<void(MafiaNet::BitStream &, const MafiaNet::PRO &) const>;

        struct TransformEntry {
            MafiaNet::NetworkID networkId;
            MafiaNet::BitSize_t bits;
            MafiaNet::BitSize_t offset;
        };

        // The minimum entry occupies four bytes on the wire. Fixed storage
        // bounds decoding without allocating for every received packet.
        struct TransformEntries {
            std::array<TransformEntry, kMaxBatchBytes / 4> storage;
            std::size_t count = 0;
            auto begin() const {
                return storage.begin();
            }
            auto end() const {
                return storage.begin() + count;
            }
            std::size_t size() const {
                return count;
            }
            const TransformEntry &operator[](std::size_t index) const {
                return storage[index];
            }
        };

        bool Write(MafiaNet::NetworkID networkId, const bool *selected, MafiaNet::BitStream *channels, MafiaNet::Time timestamp, const MafiaNet::PRO *parameters, MafiaNet::WorldId worldId, bool batchTransforms, std::size_t maxBytes, const SendPacket &send);
        void Flush(const SendPacket &send);

        // Validate the entire bounded packet before any entity is updated. Entry
        // offsets refer to the supplied stream, which must outlive their use.
        static bool ReadBatch(MafiaNet::BitStream &packet, MafiaNet::Time &timestamp, MafiaNet::WorldId &worldId, TransformEntries &entries);

      private:
        void QueueTransform(MafiaNet::NetworkID networkId, MafiaNet::BitStream &pose, MafiaNet::Time timestamp, const MafiaNet::PRO &parameters, MafiaNet::WorldId worldId, std::size_t maxBytes, const SendPacket &send);
        MafiaNet::BitStream _batch;
        MafiaNet::Time _timestamp  = 0;
        MafiaNet::WorldId _worldId = 0;
        MafiaNet::PRO _parameters {};
        std::uint16_t _count = 0;
    };
} // namespace Framework::Networking::Replication
