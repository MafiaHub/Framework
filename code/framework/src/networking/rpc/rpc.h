/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/BitStream.h>
#include <mafianet/PacketPriority.h>
#include <mafianet/RPC4Plugin.h>
#include <mafianet/types.h>

namespace Framework::Networking::RPC {
    // Native RPC4 remote procedure calls.
    //
    // There is no IRPC/IGameRPC base class and no per-type dispatch trampoline. An RPC is just a
    // plain, copyable payload struct that knows how to (de)serialize itself and exposes a stable
    // string identifier. Handlers are ordinary C function pointers — the only shape RPC4 accepts
    // (see MafiaNet::RPC4::RegisterFunction) — registered directly on the plugin and dispatched by
    // RPC4 itself. There is no longer a distinct "game RPC": an entity-scoped call simply carries a
    // MafiaNet::NetworkID field that the handler resolves through the ReplicationManager.
    //
    // A payload type T must provide:
    //   static constexpr const char *kIdentifier;            // stable, explicit, compiler-independent
    //   void Serialize(MafiaNet::BitStream *bs, bool write); // symmetric read/write
    //
    // Register a handler (a free function of the required shape) and send a payload:
    //   void OnFoo(MafiaNet::BitStream *bs, MafiaNet::Packet *p) { const auto foo = RPC::Read<Foo>(bs); ... }
    //   RPC::Register<Foo>(peer->GetRPC(), &OnFoo);
    //   RPC::Broadcast(peer->GetRPC(), foo);            // to everyone
    //   RPC::SendTo(peer->GetRPC(), foo, guid);         // to one system
    //
    // Identifiers must be unique across the whole protocol and identical on both peers. Use a
    // fully-qualified, namespaced literal (e.g. "Framework::EmitLuaEvent") rather than typeid, so it
    // stays stable across compilers and binaries.

    using Handler = void (*)(MafiaNet::BitStream *userData, MafiaNet::Packet *packet);

    // Register a handler for payload type T under its identifier.
    template <typename T>
    inline void Register(MafiaNet::RPC4 *rpc4, Handler handler) {
        rpc4->RegisterFunction(T::kIdentifier, handler);
    }

    // Decode a payload from the bitstream RPC4 handed to the handler.
    template <typename T>
    inline T Read(MafiaNet::BitStream *userData) {
        T payload {};
        payload.Serialize(userData, false);
        return payload;
    }

    // Send a payload to a single system, or broadcast to all connected systems.
    template <typename T>
    inline void Signal(MafiaNet::RPC4 *rpc4, T &payload, const MafiaNet::AddressOrGUID &target, bool broadcast, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) {
        MafiaNet::BitStream bs;
        payload.Serialize(&bs, true);
        rpc4->Signal(T::kIdentifier, &bs, priority, reliability, 0, target, broadcast, false);
    }

    template <typename T>
    inline void Broadcast(MafiaNet::RPC4 *rpc4, T &payload, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) {
        Signal(rpc4, payload, MafiaNet::UNASSIGNED_RAKNET_GUID, true, priority, reliability);
    }

    template <typename T>
    inline void SendTo(MafiaNet::RPC4 *rpc4, T &payload, MafiaNet::RakNetGUID guid, PacketPriority priority = HIGH_PRIORITY, PacketReliability reliability = RELIABLE_ORDERED) {
        Signal(rpc4, payload, guid, false, priority, reliability);
    }
} // namespace Framework::Networking::RPC
