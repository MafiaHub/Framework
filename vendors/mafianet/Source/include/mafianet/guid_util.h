/*
 *  Copyright (c) 2024, MafiaHub
 *
 *  This source code is licensed under the MIT-style license found in the
 *  license.txt file in the root directory of this source tree.
 */

/// \file
/// \brief Modern, additive value-type accessors layered over the legacy
///        RakNet/SLikeNet value types (\a RakNetGUID, \a SystemAddress).
///
/// These free functions live alongside the legacy struct methods rather than
/// inside them, so they don't touch the wire-transmitted ABI of \a RakNetGUID.
/// They favour ownership over shared static buffers and \a std::optional over
/// sentinel return values, giving callers thread-safe, leak-free primitives to
/// build cleaner APIs on top of.

#ifndef __MAFIANET_GUID_UTIL_H
#define __MAFIANET_GUID_UTIL_H

#include <optional>
#include <string>

#include "types.h"
#include "peerinterface.h"

namespace MafiaNet {

/// Return the GUID as an owned \a std::string.
///
/// Unlike the legacy \a RakNetGUID::ToString() member (which returned a pointer
/// into a rotating, process-wide static buffer and was explicitly NOT thread
/// safe), this allocates a fresh string on every call and shares no state, so
/// it is safe to call concurrently from multiple threads. It is implemented on
/// top of the thread-safe \a RakNetGUID::ToString(char*, size_t) member.
RAK_DLL_EXPORT std::string to_string(const RakNetGUID& g);

/// Look up the SystemAddress currently connected to \a g.
///
/// A thin facade over \a RakPeerInterface::GetSystemAddressFromGuid that maps
/// the legacy \a UNASSIGNED_SYSTEM_ADDRESS "none" sentinel to \a std::nullopt,
/// so callers can use ordinary optional handling instead of comparing against a
/// magic value.
RAK_DLL_EXPORT std::optional<SystemAddress> connected_address(RakPeerInterface& peer, const RakNetGUID& g);

} // namespace MafiaNet

#endif // __MAFIANET_GUID_UTIL_H
