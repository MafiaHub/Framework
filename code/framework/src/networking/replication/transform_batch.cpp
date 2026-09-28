/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "transform_batch.h"

#include <algorithm>
#include <limits>

namespace Framework::Networking::Replication {
    namespace {
        constexpr std::size_t kTimestampBytes = sizeof(MafiaNet::MessageID) + sizeof(MafiaNet::Time);
        constexpr std::size_t kHeaderBytes    = kTimestampBytes + sizeof(MafiaNet::MessageID) + sizeof(MafiaNet::WorldId) + sizeof(std::uint16_t);
        // A full-width LEB128 NetworkID and the uint16 bit count.
        constexpr std::size_t kMaxEntryOverheadBytes = 10 + sizeof(std::uint16_t);

        void PadToByte(MafiaNet::BitStream &out) {
            while (out.GetNumberOfBitsUsed() % 8 != 0) {
                out.Write(false);
            }
        }

        // Byte-oriented LEB128: one or two bytes for the small IDs the server hands out, whatever
        // BitStream's integer endianness.
        void WriteNetworkId(MafiaNet::BitStream &out, MafiaNet::NetworkID value) {
            do {
                const auto byte = static_cast<std::uint8_t>(value & 0x7F);
                value >>= 7;
                out.Write(static_cast<std::uint8_t>(byte | (value != 0 ? 0x80 : 0)));
            } while (value != 0);
        }

        bool ReadNetworkId(MafiaNet::BitStream &in, MafiaNet::NetworkID &value) {
            value = 0;
            for (int shift = 0; shift < 64; shift += 7) {
                std::uint8_t byte;
                if (!in.Read(byte) || (shift == 63 && byte > 1)) {
                    return false;
                }
                value |= static_cast<MafiaNet::NetworkID>(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0) {
                    // A trailing zero byte is a second spelling of a shorter value.
                    return shift == 0 || byte != 0;
                }
            }
            return false;
        }
    } // namespace

    bool TransformBatch::Queue(MafiaNet::NetworkID networkId, const MafiaNet::BitStream &pose, MafiaNet::Time timestamp, const MafiaNet::PRO &parameters, MafiaNet::WorldId worldId, std::size_t maxBytes, const SendPacket &send) {
        maxBytes                       = std::min(maxBytes, kMaxBytes);
        const MafiaNet::BitSize_t bits = pose.GetNumberOfBitsUsed();
        if (bits == 0 || timestamp == 0 || parameters.reliability != MafiaNet::Reliability::Unreliable || bits > std::numeric_limits<std::uint16_t>::max() || kHeaderBytes + kMaxEntryOverheadBytes + pose.GetNumberOfBytesUsed() > maxBytes) {
            return false;
        }

        MafiaNet::BitStream entry;
        WriteNetworkId(entry, networkId);
        entry.Write(static_cast<std::uint16_t>(bits));
        // Only the counted bits: the last byte of an RM3 stream can hold unrelated bits past them.
        entry.WriteBits(pose.GetData(), bits, false);
        PadToByte(entry);

        if (_count != 0 && (_timestamp != timestamp || _worldId != worldId || _parameters != parameters || _batch.GetNumberOfBytesUsed() + entry.GetNumberOfBytesUsed() > maxBytes)) {
            Flush(send);
        }
        if (_count == 0) {
            _timestamp  = timestamp;
            _worldId    = worldId;
            _parameters = parameters;
            _batch.Write(static_cast<MafiaNet::MessageID>(ID_TIMESTAMP));
            _batch.Write(timestamp);
            _batch.Write(kId);
            _batch.Write(worldId);
            _batch.Write(static_cast<std::uint16_t>(0)); // Count, patched by Flush.
        }
        _batch.WriteBits(entry.GetData(), entry.GetNumberOfBitsUsed(), false);
        ++_count;
        return true;
    }

    void TransformBatch::Flush(const SendPacket &send) {
        if (_count == 0) {
            return;
        }
        const MafiaNet::BitSize_t end = _batch.GetWriteOffset();
        _batch.SetWriteOffset(static_cast<MafiaNet::BitSize_t>(BYTES_TO_BITS(kHeaderBytes - sizeof(_count))));
        _batch.Write(_count);
        _batch.SetWriteOffset(end);
        send(_batch, _parameters);
        _batch.Reset();
        _count = 0;
    }

    bool TransformBatch::IsBatch(const unsigned char *data, unsigned int length) {
        return length > kTimestampBytes && data[0] == ID_TIMESTAMP && data[kTimestampBytes] == kId;
    }

    bool TransformBatch::Read(MafiaNet::BitStream &packet, MafiaNet::Time &timestamp, MafiaNet::WorldId &worldId, Entries &entries) {
        entries.count = 0;
        if (packet.GetNumberOfBytesUsed() > kMaxBytes) {
            return false;
        }
        MafiaNet::MessageID prefix;
        MafiaNet::MessageID id;
        std::uint16_t count;
        if (!packet.Read(prefix) || prefix != ID_TIMESTAMP || !packet.Read(timestamp) || !packet.Read(id) || id != kId || !packet.Read(worldId) || !packet.Read(count) || count == 0 || count > entries.storage.size()) {
            return false;
        }
        for (std::uint16_t i = 0; i < count; ++i) {
            Entry entry {};
            std::uint16_t bits;
            if (!ReadNetworkId(packet, entry.networkId) || entry.networkId == MafiaNet::UNASSIGNED_NETWORK_ID || !packet.Read(bits) || bits == 0) {
                return false;
            }
            const MafiaNet::BitSize_t padded = BYTES_TO_BITS(BITS_TO_BYTES(static_cast<MafiaNet::BitSize_t>(bits)));
            if (packet.GetNumberOfUnreadBits() < padded) {
                return false;
            }
            entry.bits   = bits;
            entry.offset = packet.GetReadOffset();
            packet.IgnoreBits(padded);
            entries.storage[i] = entry;
        }
        if (packet.GetNumberOfUnreadBits() != 0) {
            return false;
        }
        entries.count = count;
        return true;
    }
} // namespace Framework::Networking::Replication
