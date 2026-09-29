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
    // Packs the unreliable transforms one connection is sent in a replication pass into as few
    // messages as fit the MTU, instead of one RM3 serialize message per entity. Only the pose
    // channel is batched; everything else keeps the standard RM3 message. Wire layout, all
    // byte-aligned (see docs/replication_transport.md):
    //
    //   ID_TIMESTAMP | Time | kId | WorldId | uint16 count | count * (LEB128 NetworkID | uint16 bits | pose | pad)
    class TransformBatch final {
      public:
        // Reserved by the Framework: a game's own raw message identifiers start after it.
        static constexpr MafiaNet::MessageID kId = ID_USER_PACKET_ENUM;
        static constexpr std::size_t kMaxBytes   = 1100;

        using SendPacket = fu2::function<void(MafiaNet::BitStream &, const MafiaNet::PRO &) const>;

        struct Entry {
            MafiaNet::NetworkID networkId;
            MafiaNet::BitSize_t bits;
            // Into the packet the entry was read from, which must outlive it.
            MafiaNet::BitSize_t offset;
        };

        // The smallest entry is four bytes, which bounds a packet's entries without allocating.
        struct Entries {
            std::array<Entry, kMaxBytes / 4> storage;
            std::size_t count = 0;

            const Entry *begin() const {
                return storage.data();
            }
            const Entry *end() const {
                return storage.data() + count;
            }
            std::size_t size() const {
                return count;
            }
            const Entry &operator[](std::size_t index) const {
                return storage[index];
            }
        };

        // Adds one pose to the batch, flushing first when it would not fit or does not share the
        // batch's timestamp, world and parameters. False, with nothing queued, for a pose this
        // format cannot carry -- reliable, untimestamped or larger than the budget -- which the
        // caller then sends through RM3 as usual.
        bool Queue(MafiaNet::NetworkID networkId, const MafiaNet::BitStream &pose, MafiaNet::Time timestamp, const MafiaNet::PRO &parameters, MafiaNet::WorldId worldId, std::size_t maxBytes, const SendPacket &send);
        void Flush(const SendPacket &send);

        // Whether the packet claims to be a batch. A game packet never starts this way unless it
        // takes the reserved identifier, so anything else is left to its own handler.
        static bool IsBatch(const unsigned char *data, unsigned int length);

        // Validates the whole packet before returning any entry, so a malformed one applies nothing.
        static bool Read(MafiaNet::BitStream &packet, MafiaNet::Time &timestamp, MafiaNet::WorldId &worldId, Entries &entries);

      private:
        MafiaNet::BitStream _batch;
        MafiaNet::Time _timestamp  = 0;
        MafiaNet::WorldId _worldId = 0;
        MafiaNet::PRO _parameters {};
        std::uint16_t _count = 0;
    };
} // namespace Framework::Networking::Replication
