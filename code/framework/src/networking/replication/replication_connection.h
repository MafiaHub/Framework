/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/ReplicaManager3.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Framework::Networking::Replication {
    class NetworkEntity;
    class ReplicationManager;

    // Per-remote-system state. On the client it constructs incoming replicas (AllocReplica); on the
    // server it decides which replicas should exist on this connection (QueryReplicaList). It runs in
    // QUERY_CONNECTION_FOR_REPLICA_LIST mode, so the streaming relevance rules live in
    // QueryReplicaList rather than in Replica3::QueryConstruction/QueryDestruction.
    class ReplicationConnection final : public MafiaNet::Connection_RM3 {
      public:
        ReplicationConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID guid, ReplicationManager *manager, bool isServer);

        MafiaNet::Replica3 *AllocReplica(MafiaNet::BitStream *allocationIdBitstream, MafiaNet::ReplicaManager3 *replicaManager3) override;

        ConstructionMode QueryConstructionMode() const override {
            return QUERY_CONNECTION_FOR_REPLICA_LIST;
        }

        void QueryReplicaList(DataStructures::List<MafiaNet::Replica3 *> &newReplicasToCreate, DataStructures::List<MafiaNet::Replica3 *> &existingReplicasToDestroy) override;

        // Server: withholds the transform channel per viewer by distance band; the reliable state
        // channel always passes.
        MafiaNet::SendSerializeIfChangedResult SendSerialize(MafiaNet::Replica3 *replica, bool indicesToSend[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::BitStream serializationData[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::Time timestamp, MafiaNet::PRO sendParameters[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::RakPeerInterface *rakPeer, unsigned char worldId, MafiaNet::Time curTime) override;

        uint32_t TransformSendIntervalMs(const NetworkEntity *entity) const;

      private:
        ReplicationManager *_manager = nullptr;
        bool _isServer               = false;
        MafiaNet::PeerGuid _viewerGUID = MafiaNet::UNASSIGNED_PEER_GUID;

        // Last transform send per replica; pruned against the interest set, keys never dereferenced.
        std::unordered_map<const NetworkEntity *, MafiaNet::Time> _lastTransformSend;

        // Interest result cached against the grid generation: ReplicaManager3 calls QueryReplicaList
        // on every RakPeer::Receive(), but the grid only changes once per tick (plus removals), so
        // the query is recomputed only when the generation or the viewer changed. A removal bumps the
        // generation, which is what keeps destroyed entities out of this cache.
        std::unordered_set<NetworkEntity *> _relevant;
        // The previous _relevant, swapped aside on each recompute: the hysteresis state the grid
        // needs for the stream-out margin and the sticky budget ranking. Membership only — entries
        // are compared, never dereferenced.
        std::unordered_set<NetworkEntity *> _previousRelevant;
        NetworkEntity *_relevantViewer = nullptr;
        uint32_t _relevantGeneration   = 0;
        bool _relevantValid            = false;
    };
} // namespace Framework::Networking::Replication
