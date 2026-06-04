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

        // Keep the observer's dimension in sync with its avatar so the base filter and visible() agree.
        SetVirtualWorld(viewer->GetVirtualWorld());

        const uint64_t myGUID = GetRakNetGUID().g;

        std::unordered_set<NetworkEntity *> inRange;
        _manager->QueryRadius(viewer->position, viewer->streamRange, inRange);

        // Owned entities (avatar + e.g. the vehicle being driven) are never culled, so they don't drop
        // out as the frozen avatar's range leaves them behind.
        const auto visible = [&](NetworkEntity *entity) {
            return entity->isVisible && (entity->alwaysVisible || entity == viewer || entity->ownerGUID == myGUID || (MafiaNet::VirtualWorldsCanSee(entity->GetVirtualWorld(), GetVirtualWorld()) && inRange.contains(entity)));
        };

        // Construct candidates come only from the working set (in-range + owned + always-visible +
        // avatar), never an O(entities) scan; visible() can be satisfied only by one of these sources.
        std::unordered_set<NetworkEntity *> queued;
        const auto consider = [&](NetworkEntity *entity) {
            if (entity && visible(entity) && !HasReplicaConstructed(entity) && queued.insert(entity).second) {
                newReplicasToCreate.Push(entity, _FILE_AND_LINE_);
            }
        };
        for (NetworkEntity *entity : inRange) {
            consider(entity);
        }
        if (const auto *owned = _manager->EntitiesOwnedBy(myGUID)) {
            for (NetworkEntity *entity : *owned) {
                consider(entity);
            }
        }
        for (NetworkEntity *entity : _manager->AlwaysVisibleEntities()) {
            consider(entity);
        }
        consider(viewer);

        // Destroy side: only what this connection already has, dropping whatever turned invisible.
        DataStructures::List<MafiaNet::Replica3 *> constructed;
        GetConstructedReplicas(constructed);
        for (unsigned i = 0; i < constructed.Size(); ++i) {
            auto *entity = static_cast<NetworkEntity *>(constructed[i]);
            if (entity && !visible(entity)) {
                existingReplicasToDestroy.Push(entity, _FILE_AND_LINE_);
            }
        }
    }
} // namespace Framework::Networking::Replication
