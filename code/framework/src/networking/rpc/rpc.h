/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/BitStream.h>

#include <concepts>

namespace Framework::Networking::RPC {
    // A well-formed RPC payload: identifier + symmetric serializer.
    template <typename T>
    concept Payload = requires(T payload, MafiaNet::BitStream *bs) {
        { T::kIdentifier } -> std::convertible_to<const char *>;
        payload.Serialize(bs, true);
    };

    // An RPC is a payload struct that provides:
    //   static constexpr const char *kIdentifier;            // unique, identical on both peers
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
} // namespace Framework::Networking::RPC
