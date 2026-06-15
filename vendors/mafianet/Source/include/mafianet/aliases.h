/*
 *  Copyright (c) 2019, SLikeSoft UG (haftungsbeschraenkt)
 *
 *  This source code is  licensed under the MIT-style license found in the license.txt
 *  file in the root directory of this source tree.
 */

/// \file aliases.h
/// \brief Canonical MafiaNet type aliases over the legacy RakNet names.
///
/// The library lives in the `MafiaNet` namespace, but many public types still
/// carry their historical RakNet names. This header introduces clean, canonical
/// aliases so callers can write idiomatic MafiaNet code:
/// \code
/// MafiaNet::PeerInterface* peer = MafiaNet::PeerInterface::GetInstance();
/// \endcode
///
/// These are `using` aliases (not subclasses): each canonical name denotes the
/// exact same type as its legacy counterpart, so the two interoperate freely.
///
/// \note Aliases only — the legacy declarations are intentionally left untouched
/// and un-deprecated. A `[[deprecated]]` pass is a separate, later task.

#pragma once

#include "mafianet/peerinterface.h" // RakPeerInterface
#include "mafianet/types.h"         // RakNetGUID, UNASSIGNED_RAKNET_GUID
#include "mafianet/statistics.h"    // RakNetStatistics

namespace MafiaNet {

/// Canonical name for the main entry point, RakPeerInterface.
using PeerInterface = RakPeerInterface;

/// Canonical name for a peer's globally unique identifier, RakNetGUID.
using Guid = RakNetGUID;

/// Canonical name for the connection statistics struct, RakNetStatistics.
using Statistics = RakNetStatistics;

/// Canonical name for the unassigned-GUID sentinel, UNASSIGNED_RAKNET_GUID.
/// Bound by reference so it remains the same object as the legacy sentinel.
inline const Guid& UnassignedGuid = UNASSIGNED_RAKNET_GUID;

} // namespace MafiaNet
