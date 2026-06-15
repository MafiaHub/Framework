/*
 *  Copyright (c) 2026, MafiaHub
 *
 *  This source code is licensed under the MIT-style license found in the
 *  license.txt file in the root directory of this source tree.
 */

/// \file PeerHandle.h
/// \brief RAII handles for the two core MafiaNet resources.
///
/// \ref MafiaNet::Peer owns a RakPeerInterface instance (GetInstance/DestroyInstance);
/// \ref MafiaNet::PacketPtr owns a Packet received from it (Receive/DeallocatePacket).
/// Both are movable, non-copyable and exception-safe. This is purely additive —
/// the raw factory/Receive API remains available and unchanged.

#pragma once

#include "mafianet/peerinterface.h" // RakPeerInterface, Receive, DeallocatePacket, GetInstance, DestroyInstance
#include "mafianet/types.h"         // Packet
#include "mafianet/Export.h"        // RAK_DLL_EXPORT

namespace MafiaNet {

/// \brief Owning handle for a Packet returned by RakPeerInterface::Receive().
/// Calls DeallocatePacket() in its destructor. Movable, non-copyable.
class RAK_DLL_EXPORT PacketPtr {
public:
    /// Takes ownership of \a p, which must have been produced by \a owner.
    PacketPtr(RakPeerInterface* owner, Packet* p) : owner_(owner), p_(p) {}
    ~PacketPtr();

    PacketPtr(PacketPtr&& o) noexcept;
    PacketPtr& operator=(PacketPtr&& o) noexcept;
    PacketPtr(const PacketPtr&) = delete;
    PacketPtr& operator=(const PacketPtr&) = delete;

    Packet* operator->() const { return p_; }
    Packet& operator*()  const { return *p_; }
    Packet* get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

    /// Message identifier, accounting for an ID_TIMESTAMP prefix.
    /// Returns 255 when the packet is null or empty.
    unsigned char id() const;

private:
    RakPeerInterface* owner_;
    Packet* p_;
};

/// \brief Owning handle for a RakPeerInterface instance.
/// Calls DestroyInstance() in its destructor. Movable, non-copyable.
class RAK_DLL_EXPORT Peer {
public:
    Peer() : raw_(RakPeerInterface::GetInstance()) {}
    ~Peer();

    Peer(Peer&& o) noexcept;
    Peer& operator=(Peer&& o) noexcept;
    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;

    RakPeerInterface* operator->() const { return raw_; }
    RakPeerInterface* get() const { return raw_; }
    /// False only for a moved-from (and thus empty) handle.
    explicit operator bool() const { return raw_ != nullptr; }

    /// Receive the next queued packet wrapped in a PacketPtr (may be empty).
    PacketPtr receive();

private:
    RakPeerInterface* raw_;
};

} // namespace MafiaNet
