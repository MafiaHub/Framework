/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "replication_writer.h"

#include <algorithm>
#include <array>
#include <limits>

namespace Framework::Networking::Replication {
    namespace {
        constexpr std::size_t kBatchHeaderBytes = 2 + sizeof(MafiaNet::Time) + sizeof(MafiaNet::WorldId) + sizeof(std::uint16_t);

        void PadToByte(MafiaNet::BitStream &out) {
            while (out.GetNumberOfBitsUsed() % 8 != 0) {
                out.Write(false);
            }
        }

        void WriteNetworkId(MafiaNet::BitStream &out, MafiaNet::NetworkID value) {
            // Byte-oriented varint: unlike WriteCompressed<uint64_t>, this is
            // compact for small IDs even when BitStream swaps integer endianness.
            do {
                auto byte = static_cast<std::uint8_t>(value & 0x7F);
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
                    return shift == 0 || byte != 0; // Reject noncanonical encodings.
                }
            }
            return false;
        }

        void WriteEntry(MafiaNet::BitStream &out, MafiaNet::NetworkID networkId, MafiaNet::BitStream &pose) {
            WriteNetworkId(out, networkId);
            out.Write(static_cast<std::uint16_t>(pose.GetNumberOfBitsUsed()));
            // Write only the meaningful bits, then zero padding: the last byte
            // of an RM3 stream can contain unrelated bits outside its bit count.
            out.WriteBits(pose.GetData(), pose.GetNumberOfBitsUsed(), false);
            PadToByte(out);
        }
    } // namespace

    bool ReplicationWriter::Write(MafiaNet::NetworkID networkId, const bool *selected, MafiaNet::BitStream *channels, MafiaNet::Time timestamp, const MafiaNet::PRO *parameters, MafiaNet::WorldId worldId, bool batchTransforms, std::size_t maxBytes, const SendPacket &send) {
        std::array<bool, MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS> pending {};
        bool wrote = false;
        for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
            pending[i] = selected[i] && channels[i].GetNumberOfBitsUsed() != 0;
            wrote      = wrote || pending[i];
        }

        maxBytes = std::min(maxBytes, kMaxBatchBytes);
        // Oversize/custom transforms retain the standard RM3 fragmentation path.
        // The conservative entry allowance covers a full-width compressed ID.
        if (pending[0] && batchTransforms && timestamp != 0 && parameters[0].reliability == MafiaNet::Reliability::Unreliable && channels[0].GetNumberOfBitsUsed() <= std::numeric_limits<std::uint16_t>::max()
            && kBatchHeaderBytes + 12 + channels[0].GetNumberOfBytesUsed() <= maxBytes) {
            QueueTransform(networkId, channels[0], timestamp, parameters[0], worldId, maxBytes, send);
            pending[0] = false;
        }

        // Group selected channels by delivery parameters. Testing selection here
        // is essential: RM3 retains old bytes even after clearing indicesToSend.
        for (int first = 0; first < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++first) {
            if (!pending[first]) {
                continue;
            }
            MafiaNet::BitStream out;
            if (timestamp != 0) {
                out.Write(static_cast<MafiaNet::MessageID>(ID_TIMESTAMP));
                out.Write(timestamp);
            }
            out.Write(static_cast<MafiaNet::MessageID>(ID_REPLICA_MANAGER_SERIALIZE));
            out.Write(worldId);
            out.Write(networkId);
            for (int i = 0; i < MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS; ++i) {
                const bool include = pending[i] && parameters[i] == parameters[first];
                out.Write(include);
                if (include) {
                    out.WriteCompressed(channels[i].GetNumberOfBitsUsed());
                    PadToByte(out);
                    out.WriteBits(channels[i].GetData(), channels[i].GetNumberOfBitsUsed(), false);
                    pending[i] = false;
                }
            }
            send(out, parameters[first]);
        }
        return wrote;
    }

    void ReplicationWriter::QueueTransform(MafiaNet::NetworkID networkId, MafiaNet::BitStream &pose, MafiaNet::Time timestamp, const MafiaNet::PRO &parameters, MafiaNet::WorldId worldId, std::size_t maxBytes, const SendPacket &send) {
        MafiaNet::BitStream entry;
        WriteEntry(entry, networkId, pose);
        if (_count != 0 && (_timestamp != timestamp || _worldId != worldId || _parameters != parameters || _batch.GetNumberOfBytesUsed() + entry.GetNumberOfBytesUsed() > maxBytes)) {
            Flush(send);
        }
        if (_count == 0) {
            _timestamp  = timestamp;
            _worldId    = worldId;
            _parameters = parameters;
            _batch.Write(static_cast<MafiaNet::MessageID>(ID_TIMESTAMP));
            _batch.Write(timestamp);
            _batch.Write(kTransformBatchId);
            _batch.Write(worldId);
            _batch.Write(static_cast<std::uint16_t>(0));
        }
        _batch.WriteBits(entry.GetData(), entry.GetNumberOfBitsUsed(), false);
        ++_count;
    }

    void ReplicationWriter::Flush(const SendPacket &send) {
        if (_count == 0) {
            return;
        }
        const auto end = _batch.GetWriteOffset();
        _batch.SetWriteOffset(static_cast<MafiaNet::BitSize_t>((kBatchHeaderBytes - sizeof(_count)) * 8));
        _batch.Write(_count);
        _batch.SetWriteOffset(end);
        send(_batch, _parameters);
        _batch.Reset();
        _count = 0;
    }

    bool ReplicationWriter::ReadBatch(MafiaNet::BitStream &packet, MafiaNet::Time &timestamp, MafiaNet::WorldId &worldId, TransformEntries &entries) {
        entries.count = 0;
        if (packet.GetNumberOfBytesUsed() > kMaxBatchBytes) {
            return false;
        }
        MafiaNet::MessageID prefix, id;
        std::uint16_t count;
        if (!packet.Read(prefix) || prefix != ID_TIMESTAMP || !packet.Read(timestamp) || !packet.Read(id) || id != kTransformBatchId || !packet.Read(worldId) || !packet.Read(count) || count == 0 || count > kMaxBatchBytes / 4) {
            return false;
        }
        for (std::uint16_t i = 0; i < count; ++i) {
            TransformEntry entry {};
            std::uint16_t bits;
            if (!ReadNetworkId(packet, entry.networkId) || entry.networkId == MafiaNet::UNASSIGNED_NETWORK_ID) {
                return false;
            }
            if (!packet.Read(bits) || bits == 0) {
                return false;
            }
            entry.bits                       = bits;
            entry.offset                     = packet.GetReadOffset();
            const MafiaNet::BitSize_t padded = (static_cast<MafiaNet::BitSize_t>(bits) + 7) / 8 * 8;
            if (packet.GetNumberOfUnreadBits() < padded) {
                return false;
            }
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
