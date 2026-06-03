/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "replication_connection.h"

#include "entity_factory.h"
#include "network_entity.h"
#include "replication_manager.h"

#include <unordered_set>

namespace Framework::Networking::Replication {
    ReplicationConnection::ReplicationConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID guid, ReplicationManager *manager, bool isServer)
        : Connection_RM3(systemAddress, guid), _manager(manager), _isServer(isServer) {}

    MafiaNet::Replica3 *ReplicationConnection::AllocReplica(MafiaNet::BitStream *allocationIdBitstream, MafiaNet::ReplicaManager3 *) {
        uint32_t typeId = 0;
        allocationIdBitstream->Read(typeId);
        // The instance's state is populated by DeserializeConstruction (called immediately after);
        // any backing game object is requested from NetworkEntity::OnConstructed.
        return EntityFactory::Get().Create(typeId);
    }

    void ReplicationConnection::QueryReplicaList(DataStructures::List<MafiaNet::Replica3 *> &newReplicasToCreate, DataStructures::List<MafiaNet::Replica3 *> &existingReplicasToDestroy) {
        // Only the server decides what exists on a remote system.
        if (!_isServer || !_manager) {
            return;
        }

        NetworkEntity *viewer = _manager->GetViewer(GetRakNetGUID().g);
        if (!viewer) {
            // Connection not yet associated with a controlled entity (still handshaking).
            return;
        }

        // The observer perceives its avatar's dimension; keep them in sync so the base
        // QuerySerialization filter and the construction check below agree.
        SetVirtualWorld(viewer->GetVirtualWorld());

        // Spatial interest set around the viewer. A hash set so the per-entity membership test below
        // is O(1): QueryReplicaList already walks every entity, so a linear scan here would make the
        // whole pass O(entities × in-range) per connection, per network update.
        std::unordered_set<NetworkEntity *> inRange;
        _manager->QueryRadius(viewer->position, viewer->streamRange, inRange);

        _manager->ForEachEntity([&](NetworkEntity *entity) {
            // The owner DOES receive its own entity (so it has the replica to serialize upstream and
            // to recognize it as the local player). The server simply withholds serialize *updates*
            // to the owner via NetworkEntity::QuerySerializationWithinWorld — construction still flows.
            const bool visible = entity->isVisible && (entity->alwaysVisible || entity == viewer || (MafiaNet::VirtualWorldsCanSee(entity->GetVirtualWorld(), GetVirtualWorld()) && inRange.contains(entity)));

            const bool constructed = HasReplicaConstructed(entity);
            if (visible && !constructed) {
                newReplicasToCreate.Push(entity, _FILE_AND_LINE_);
            }
            else if (!visible && constructed) {
                existingReplicasToDestroy.Push(entity, _FILE_AND_LINE_);
            }
        });
    }
} // namespace Framework::Networking::Replication
