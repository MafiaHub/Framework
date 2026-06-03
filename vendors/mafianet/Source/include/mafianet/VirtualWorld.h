/*
 *  Copyright (c) 2024, MafiaHub
 *
 *  This source code is licensed under the MIT-style license found in the
 *  license.txt file in the root directory of this source tree.
 */

/// \file
/// \brief Lightweight per-entity / per-observer "virtual world" (dimension) tag.
///
/// A virtual world is a runtime visibility scope layered on top of
/// ReplicaManager3. Unlike an RM3 WorldId (a heavyweight, separate instance
/// with its own NetworkIDManager and connection/replica lists), a virtual world
/// is just an id carried by an entity (Replica3) and by an observer
/// (Connection_RM3) while both remain on the same connection and same RM3 world.
///
/// Two subjects can see each other when they share the same virtual world id,
/// or when either side is the reserved \a global value (visible everywhere).
/// This is the SA-MP `SetPlayerVirtualWorld` / routing-bucket model: drop a
/// player into a dimension at runtime so they only see players, vehicles, and
/// objects in that dimension, and vanish for the rest.

#ifndef __VIRTUAL_WORLD_H
#define __VIRTUAL_WORLD_H

#include <stdint.h>

namespace MafiaNet
{

/// \brief Identifier for a virtual world (dimension).
/// \details A 32-bit id, so the number of simultaneous dimensions is effectively
/// unbounded (unlike RM3's 8-bit WorldId). \a VIRTUAL_WORLD_DEFAULT is the main
/// world; \a VIRTUAL_WORLD_GLOBAL means "visible in every virtual world".
/// \ingroup REPLICA_MANAGER_GROUP3
typedef uint32_t VirtualWorldId;

/// The default virtual world every entity and observer starts in (the overworld).
static const VirtualWorldId VIRTUAL_WORLD_DEFAULT = 0;

/// Reserved sentinel: an entity in this world is visible to every observer, and
/// an observer in this world sees entities in every world. Useful for shared
/// world geometry, global NPCs, or admins/spectators.
static const VirtualWorldId VIRTUAL_WORLD_GLOBAL = 0xFFFFFFFF;

/// \brief Returns whether two subjects in the given virtual worlds can see each other.
/// \details Visibility is symmetric: equal ids, or either side being
/// \a VIRTUAL_WORLD_GLOBAL.
/// \param[in] a First subject's virtual world
/// \param[in] b Second subject's virtual world
/// \return True if visible to one another, false otherwise
inline bool VirtualWorldsCanSee(VirtualWorldId a, VirtualWorldId b)
{
	return a == b || a == VIRTUAL_WORLD_GLOBAL || b == VIRTUAL_WORLD_GLOBAL;
}

} // namespace MafiaNet

#endif // __VIRTUAL_WORLD_H
