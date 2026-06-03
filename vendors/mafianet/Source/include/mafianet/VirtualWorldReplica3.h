/*
 *  Copyright (c) 2024, MafiaHub
 *
 *  This source code is licensed under the MIT-style license found in the
 *  license.txt file in the root directory of this source tree.
 */

/// \file
/// \brief Replica3 base class that scopes visibility by virtual world.

#include "NativeFeatureIncludes.h"
#if _RAKNET_SUPPORT_ReplicaManager3==1

#ifndef __VIRTUAL_WORLD_REPLICA_3_H
#define __VIRTUAL_WORLD_REPLICA_3_H

#include "ReplicaManager3.h"
#include "VirtualWorld.h"

namespace MafiaNet
{

/// \brief A Replica3 that is only visible to observers sharing its virtual world.
/// \details Derive your networked entities (players, vehicles, objects) from this
/// instead of Replica3 to get runtime dimension scoping for free. The virtual
/// world filter is applied in QueryConstruction / QueryDestruction /
/// QuerySerialization; when the observer can see this entity
/// (\ref VirtualWorldsCanSee), the call delegates to the *WithinWorld() hooks,
/// which you implement with the usual topology defaults (server, client, or
/// peer-to-peer).
///
/// Runtime switching just works in the default construction mode
/// (QUERY_REPLICA_FOR_CONSTRUCTION_AND_DESTRUCTION): when an entity's or an
/// observer's virtual world changes, RM3's next Update() re-queries and spawns
/// the newly-visible entities in and the no-longer-visible ones out
/// automatically — no Pop/Push of connections, no manual destruction broadcast.
///
/// \note If you override QueryConstructionMode() to return
/// QUERY_CONNECTION_FOR_REPLICA_LIST, these hooks are not called and you are
/// responsible for applying VirtualWorldsCanSee() in QueryReplicaList() yourself.
/// \ingroup REPLICA_MANAGER_GROUP3
class RAK_DLL_EXPORT VirtualWorldReplica3 : public Replica3
{
public:
	VirtualWorldReplica3() : virtualWorld(VIRTUAL_WORLD_DEFAULT) {}
	virtual ~VirtualWorldReplica3() {}

	/// \brief Set the virtual world this entity exists in.
	void SetVirtualWorld(VirtualWorldId vw) { virtualWorld = vw; }

	/// \return The virtual world this entity exists in.
	VirtualWorldId GetVirtualWorld(void) const { return virtualWorld; }

	// --- Replica3 visibility hooks: filter by virtual world, then delegate. ---
	//
	// The virtual world filter is only applied by the AUTHORITY for a given
	// (entity, connection) pair -- i.e. the system that would actually construct
	// this entity toward that connection. A downloaded copy on a non-authority
	// (e.g. a server-owned object on a client) must defer entirely to the
	// topology default; otherwise its own connection (whose virtual world is not
	// meaningfully set on that side) would look "different" and the copy would
	// send a spurious destruction upstream, deleting the entity at its owner.
	//
	// Authority is detected as QueryConstructionWithinWorld() == RM3CS_SEND_CONSTRUCTION
	// ONLY. RM3CS_ALREADY_EXISTS_REMOTELY is deliberately NOT treated as
	// authoritative: the same state is returned both by a currently-authoritative
	// static owner AND by genuinely non-authoritative peers
	// (R3P2PM_MULTI_OWNER_NOT_CURRENTLY_AUTHORITATIVE,
	// R3P2PM_STATIC_OBJECT_NOT_CURRENTLY_AUTHORITATIVE), and the two cannot be
	// distinguished from the construction state alone -- treating it as authority
	// would re-introduce the spurious-upstream-destruction bug. Consequently
	// "already exists remotely" static objects are not virtual-world filtered;
	// such objects (typically global level geometry) should be scoped by other
	// means if needed.

	virtual RM3ConstructionState QueryConstruction(Connection_RM3 *destinationConnection, ReplicaManager3 *replicaManager3)
	{
		RM3ConstructionState within = QueryConstructionWithinWorld(destinationConnection, replicaManager3);
		if (within == RM3CS_SEND_CONSTRUCTION && !VirtualWorldsCanSee(virtualWorld, destinationConnection->GetVirtualWorld()))
			return RM3CS_NO_ACTION; // authority, but not in this observer's world (re-queried next tick)
		return within;
	}

	virtual RM3DestructionState QueryDestruction(Connection_RM3 *destinationConnection, ReplicaManager3 *replicaManager3)
	{
		bool authoritative = QueryConstructionWithinWorld(destinationConnection, replicaManager3) == RM3CS_SEND_CONSTRUCTION;
		if (authoritative && !VirtualWorldsCanSee(virtualWorld, destinationConnection->GetVirtualWorld()))
			return RM3DS_SEND_DESTRUCTION; // left this observer's world -> despawn it for them
		return QueryDestructionWithinWorld(destinationConnection, replicaManager3);
	}

	virtual RM3QuerySerializationResult QuerySerialization(Connection_RM3 *destinationConnection)
	{
		bool authoritative = QueryConstructionWithinWorld(destinationConnection, replicaManager) == RM3CS_SEND_CONSTRUCTION;
		if (authoritative && !VirtualWorldsCanSee(virtualWorld, destinationConnection->GetVirtualWorld()))
			return RM3QSR_DO_NOT_CALL_SERIALIZE; // skipped while out of world (re-queried next tick)
		return QuerySerializationWithinWorld(destinationConnection);
	}

protected:
	/// \brief Construction decision once the observer is known to be in this entity's world.
	/// \details Implement with one of QueryConstruction_ServerConstruction(),
	/// QueryConstruction_ClientConstruction(), or QueryConstruction_PeerToPeer().
	virtual RM3ConstructionState QueryConstructionWithinWorld(Connection_RM3 *destinationConnection, ReplicaManager3 *replicaManager3) = 0;

	/// \brief Destruction decision once the observer is known to be in this entity's world.
	/// \details Defaults to RM3DS_NO_ACTION (do nothing, keep querying) so that a
	/// later virtual world change is still able to despawn the entity. Override
	/// only if you also drive per-connection destruction from within a world.
	virtual RM3DestructionState QueryDestructionWithinWorld(Connection_RM3 *destinationConnection, ReplicaManager3 *replicaManager3)
	{
		(void)destinationConnection;
		(void)replicaManager3;
		return RM3DS_NO_ACTION;
	}

	/// \brief Serialization decision once the observer is known to be in this entity's world.
	/// \details Implement with one of QuerySerialization_ServerSerializable(),
	/// QuerySerialization_ClientSerializable(), or QuerySerialization_PeerToPeer().
	virtual RM3QuerySerializationResult QuerySerializationWithinWorld(Connection_RM3 *destinationConnection) = 0;

	VirtualWorldId virtualWorld;
};

} // namespace MafiaNet

#endif // __VIRTUAL_WORLD_REPLICA_3_H

#endif // _RAKNET_SUPPORT_ReplicaManager3
