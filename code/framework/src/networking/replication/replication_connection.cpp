/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "replication_connection.h"

#include "entity_registry.h"
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
        return EntityRegistry::Get().Create(typeId);
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

        // Keep the observer's dimension in sync with its avatar so the base filter and the grid's
        // relevance rule agree.
        SetVirtualWorld(viewer->GetVirtualWorld());

        // The relevance rule itself lives in InterestGrid::CollectVisible; here we only reconcile the
        // resulting set against what this connection has already constructed.
        std::unordered_set<NetworkEntity *> relevant;
        _manager->CollectInterest(viewer, GetRakNetGUID().g, relevant);

        for (NetworkEntity *entity : relevant) {
            if (!HasReplicaConstructed(entity)) {
                newReplicasToCreate.Push(entity, _FILE_AND_LINE_);
            }
        }

        // Destroy side: only what this connection already has, dropping whatever is no longer relevant.
        DataStructures::List<MafiaNet::Replica3 *> constructed;
        GetConstructedReplicas(constructed);
        for (unsigned i = 0; i < constructed.Size(); ++i) {
            auto *entity = static_cast<NetworkEntity *>(constructed[i]);
            if (entity && !relevant.contains(entity)) {
                existingReplicasToDestroy.Push(entity, _FILE_AND_LINE_);
            }
        }
    }
} // namespace Framework::Networking::Replication
