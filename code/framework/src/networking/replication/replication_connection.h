/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/ReplicaManager3.h>

namespace Framework::Networking::Replication {
    class ReplicationManager;

    // Per-remote-system state. On the client it constructs incoming replicas (AllocReplica); on the
    // server it answers which replicas should exist on this connection (QueryReplicaList), which is
    // where the streaming relevance rules live now that QUERY_CONNECTION_FOR_REPLICA_LIST is used
    // (so Replica3::QueryConstruction/QueryDestruction are intentionally bypassed).
    class ReplicationConnection final : public MafiaNet::Connection_RM3 {
      public:
        ReplicationConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID guid, ReplicationManager *manager, bool isServer);

        MafiaNet::Replica3 *AllocReplica(MafiaNet::BitStream *allocationIdBitstream, MafiaNet::ReplicaManager3 *replicaManager3) override;

        ConstructionMode QueryConstructionMode() const override {
            return QUERY_CONNECTION_FOR_REPLICA_LIST;
        }

        void QueryReplicaList(DataStructures::List<MafiaNet::Replica3 *> &newReplicasToCreate, DataStructures::List<MafiaNet::Replica3 *> &existingReplicasToDestroy) override;

      private:
        ReplicationManager *_manager = nullptr;
        bool _isServer               = false;
    };
} // namespace Framework::Networking::Replication
