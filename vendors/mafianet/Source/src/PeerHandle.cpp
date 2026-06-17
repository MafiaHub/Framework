/*
 *  Copyright (c) 2026, MafiaHub
 *
 *  This source code is licensed under the MIT-style license found in the
 *  license.txt file in the root directory of this source tree.
 */

#include "mafianet/PeerHandle.h"

#include <type_traits>

#include "mafianet/MessageIdentifiers.h" // ID_TIMESTAMP

namespace MafiaNet {

// --- Compile-time contract: movable, non-copyable (acceptance criterion #1) ---
static_assert(std::is_move_constructible<PacketPtr>::value, "PacketPtr must be move-constructible");
static_assert(std::is_move_assignable<PacketPtr>::value,    "PacketPtr must be move-assignable");
static_assert(!std::is_copy_constructible<PacketPtr>::value, "PacketPtr must NOT be copy-constructible");
static_assert(!std::is_copy_assignable<PacketPtr>::value,    "PacketPtr must NOT be copy-assignable");
static_assert(std::is_move_constructible<Peer>::value, "Peer must be move-constructible");
static_assert(std::is_move_assignable<Peer>::value,    "Peer must be move-assignable");
static_assert(!std::is_copy_constructible<Peer>::value, "Peer must NOT be copy-constructible");
static_assert(!std::is_copy_assignable<Peer>::value,    "Peer must NOT be copy-assignable");

// --- PacketPtr ---

PacketPtr::~PacketPtr() {
    if (p_)
        owner_->DeallocatePacket(p_);
}

PacketPtr::PacketPtr(PacketPtr&& o) noexcept : owner_(o.owner_), p_(o.p_) {
    o.p_ = nullptr;
}

PacketPtr& PacketPtr::operator=(PacketPtr&& o) noexcept {
    if (this != &o) {
        if (p_)
            owner_->DeallocatePacket(p_);
        owner_ = o.owner_;
        p_ = o.p_;
        o.p_ = nullptr;
    }
    return *this;
}

unsigned char PacketPtr::id() const {
    // Mirrors GetPacketIdentifier from the samples: an ID_TIMESTAMP prefix is
    // followed by the MessageID + Time, then the real identifier byte.
    if (p_ == nullptr || p_->length == 0)
        return 255;

    if (static_cast<unsigned char>(p_->data[0]) == ID_TIMESTAMP) {
        // Guard against a truncated timestamp packet: relying on RakAssert alone
        // would be an out-of-bounds read in release builds.
        RakAssert(p_->length > sizeof(MessageID) + sizeof(Time));
        if (p_->length <= sizeof(MessageID) + sizeof(Time))
            return 255;
        return static_cast<unsigned char>(p_->data[sizeof(MessageID) + sizeof(Time)]);
    }
    return static_cast<unsigned char>(p_->data[0]);
}

// --- Peer ---

Peer::~Peer() {
    if (raw_)
        RakPeerInterface::DestroyInstance(raw_);
}

Peer::Peer(Peer&& o) noexcept : raw_(o.raw_) {
    o.raw_ = nullptr;
}

Peer& Peer::operator=(Peer&& o) noexcept {
    if (this != &o) {
        if (raw_)
            RakPeerInterface::DestroyInstance(raw_);
        raw_ = o.raw_;
        o.raw_ = nullptr;
    }
    return *this;
}

PacketPtr Peer::receive() {
    // A moved-from Peer is empty; never dereference a null instance.
    return raw_ ? PacketPtr(raw_, raw_->Receive()) : PacketPtr(nullptr, nullptr);
}

} // namespace MafiaNet
