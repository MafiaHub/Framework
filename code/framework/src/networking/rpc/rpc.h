/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "rpc_identifier.h"

#include <mafianet/bit_stream.h>

#include <concepts>
#include <cstddef>
#include <cstring>
#include <vector>

namespace Framework::Networking::RPC {
    // A well-formed RPC payload: identifier + symmetric serializer.
    template <typename T>
    concept Payload = requires(T payload, MafiaNet::BitStream *bs) {
        { T::kIdentifier } -> std::convertible_to<Identifier>;
        payload.Serialize(bs, true);
    };

    // An RPC is a payload struct that provides:
    //   static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Mod::Name"); // unique on both peers
    //   void Serialize(MafiaNet::BitStream *bs, bool write); // symmetric read/write
    //
    // Send with NetworkPeer::BroadcastRPC / SendRPC. Receive with RegisterRPC<T> on the concrete peer:
    // NetworkClient's handler is given the decoded payload and the packet, NetworkServer's the payload,
    // the Sender and the World. To target a specific entity, give the payload a network id field; a
    // server handler takes it from the world by handle, a client through ClientEntities.
    template <typename T>
    inline T Read(MafiaNet::BitStream *bs) {
        T payload {};
        payload.Serialize(bs, false);
        return payload;
    }

    namespace detail {
        // How far past the end of what was sent a read may reach and still be noticed: see ReadChecked.
        inline constexpr std::size_t kReadPadBytes = std::size_t {1} << 20;

        // What is left of a stream, copied and followed by kReadPadBytes of zeros, to decode from. A
        // BitStream tells only the caller of each read that it came up short, and a payload's Serialize
        // drops that answer; read from here instead, no read comes up short, and where the reading
        // stopped says whether it went past what the sender sent.
        class PaddedRead final {
          public:
            // Copied from the byte the reader stands in, and read from the same bit of it: a Serialize
            // that aligns to a byte boundary means the sender's boundaries.
            explicit PaddedRead(MafiaNet::BitStream *source): _source(source), _start(source->GetReadOffset()), _lead(_start & 7), _bits(source->GetNumberOfUnreadBits()), _bytes(BITS_TO_BYTES(_lead + _bits)), _buffer(Buffer()) {
                if (_buffer.size() < _bytes + kReadPadBytes) {
                    _buffer.resize(_bytes + kReadPadBytes, 0);
                }
                if (_bytes > 0) {
                    std::memcpy(_buffer.data(), _source->GetData() + (_start >> 3), _bytes);
                }
            }
            // Leaves the buffer all zeros again for the next decode on this thread.
            ~PaddedRead() {
                std::memset(_buffer.data(), 0, _bytes);
            }
            PaddedRead(const PaddedRead &)            = delete;
            PaddedRead &operator=(const PaddedRead &) = delete;

            // Decodes `payload`, once, and leaves the source where the read stopped, or at its end.
            // False if the payload's Serialize read past the end of the source.
            template <typename T>
            bool Into(T &payload) {
                MafiaNet::BitStream padded(_buffer.data(), static_cast<unsigned int>(_bytes + kReadPadBytes), false);
                padded.SetReadOffset(_lead);
                payload.Serialize(&padded, false);
                const MafiaNet::BitSize_t stopped  = padded.GetReadOffset();
                const MafiaNet::BitSize_t consumed = stopped - _lead;
                _source->SetReadOffset(_start + (consumed < _bits ? consumed : _bits));
                // To the end of its last byte: aligning a read to a byte boundary is not reading past it.
                return stopped <= BYTES_TO_BITS(_bytes);
            }

          private:
            // One per thread that decodes, kept between calls.
            static std::vector<unsigned char> &Buffer() {
                thread_local std::vector<unsigned char> buffer;
                return buffer;
            }

            MafiaNet::BitStream *_source;
            MafiaNet::BitSize_t _start;
            MafiaNet::BitSize_t _lead;
            MafiaNet::BitSize_t _bits;
            std::size_t _bytes;
            std::vector<unsigned char> &_buffer;
        };
    } // namespace detail

    // Decodes a payload for a peer that does not trust the sender, and says whether the stream held
    // everything its Serialize read: false when a read asked for bits the sender did not send, which
    // without this check leaves that field, and those after it, at their defaults. Serialize runs once,
    // reading. Bits left unread are not an error, and the stream is left after the last bit read.
    //
    // The limit: one read that asks for more than kReadPadBytes beyond the end (a length taken off the
    // wire and believed, a megabyte or more) fails as it would on the bare stream, unnoticed here
    // unless a later read then runs past the end. A payload that reads such lengths checks them itself.
    template <typename T>
    inline bool ReadChecked(MafiaNet::BitStream *bs, T &payload) {
        detail::PaddedRead read(bs);
        return read.Into(payload);
    }
} // namespace Framework::Networking::RPC
