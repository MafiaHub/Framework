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
    // Register a handler with NetworkPeer::RegisterRPC<T> and send with NetworkPeer::BroadcastRPC /
    // SendRPC (and NetworkServer::BroadcastRPCExcept). A handler is a function of shape
    // void(MafiaNet::BitStream *, MafiaNet::Packet *); decode its payload with Read<T>. To target a
    // specific entity, give the payload a MafiaNet::NetworkID field and resolve it through the
    // ReplicationManager in the handler.
    template <typename T>
    inline T Read(MafiaNet::BitStream *bs) {
        T payload {};
        payload.Serialize(bs, false);
        return payload;
    }

    // Decodes a payload and says whether the stream held all of it, for a peer that does not trust the
    // sender. A BitStream reports a short read to whoever made that read and to nobody else, and
    // Serialize returns nothing, so a truncated payload decodes to defaults and to whatever was half
    // read. Serialize is symmetric: the decoded payload is written back, and it was read whole exactly
    // when that gives the bits the read consumed. Anything else is refused, an encoding no Serialize
    // would have written included. Bits after the payload are left unread, as Read leaves them.
    template <typename T>
    inline bool ReadWhole(MafiaNet::BitStream *bs, T &payload) {
        const MafiaNet::BitSize_t start = bs->GetReadOffset();
        payload.Serialize(bs, false);
        const MafiaNet::BitSize_t end = bs->GetReadOffset();
        MafiaNet::BitStream written;
        payload.Serialize(&written, true);
        const MafiaNet::BitSize_t bits = written.GetNumberOfBitsUsed();
        if (end < start || end - start != bits || end > bs->GetNumberOfBitsUsed()) {
            return false;
        }
        if (bits == 0) {
            return true;
        }
        MafiaNet::BitStream consumed;
        bs->SetReadOffset(start);
        if (!bs->Read(&consumed, bits) || consumed.GetNumberOfBitsUsed() != bits) {
            return false;
        }
        const unsigned char *a          = written.GetData();
        const unsigned char *b          = consumed.GetData();
        const MafiaNet::BitSize_t whole = bits / 8;
        for (MafiaNet::BitSize_t i = 0; i < whole; ++i) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        const unsigned int rest = static_cast<unsigned int>(bits % 8);
        return rest == 0 || ((a[whole] ^ b[whole]) >> (8 - rest)) == 0;
    }
} // namespace Framework::Networking::RPC
