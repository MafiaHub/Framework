/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "interest_grid.h"
#include "transform_batch.h"

#include <mafianet/replica_manager3.h>

#include <cstdint>
#include <vector>

namespace Framework::Networking::Replication {
    class NetworkEntity;
    class ReplicationManager;

    // Per-remote-system state. On the client it constructs incoming replicas (AllocReplica); on the
    // server it decides which replicas should exist on this connection (QueryReplicaList). It runs in
    // QUERY_CONNECTION_FOR_REPLICA_LIST mode, so the streaming relevance rules live in
    // QueryReplicaList rather than in Replica3::QueryConstruction/QueryDestruction.
    //
    // ReplicaManager3 asks QueryReplicaList every pass, so the answer is incremental: a recomputed
    // interest set (on this viewer's turn, see ReplicationManager::RebuildInterest, or at once when
    // something concerning this viewer alone changed) is diffed against what ReplicaManager3 has
    // constructed here; between those, only entities the manager asked every viewer to re-evaluate
    // (created, or their audience widened) are looked at. A pass in which neither happened is free.
    class ReplicationConnection final: public MafiaNet::Connection_RM3 {
      public:
        ReplicationConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID guid, ReplicationManager *manager, bool isServer);

        MafiaNet::Replica3 *AllocReplica(MafiaNet::BitStream *allocationIdBitstream, MafiaNet::ReplicaManager3 *replicaManager3) override;

        ConstructionMode QueryConstructionMode() const override {
            return QUERY_CONNECTION_FOR_REPLICA_LIST;
        }

        void QueryReplicaList(DataStructures::List<MafiaNet::Replica3 *> &newReplicasToCreate, DataStructures::List<MafiaNet::Replica3 *> &existingReplicasToDestroy) override;

        // Withholds the transform channel per viewer by distance band (server) and, with batching
        // enabled, queues it into this connection's batch; the reliable state channel always passes.
        MafiaNet::SendSerializeIfChangedResult SendSerialize(MafiaNet::Replica3 *replica, bool indicesToSend[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::BitStream serializationData[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::Time timestamp,
            MafiaNet::PRO sendParameters[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::RakPeerInterface *rakPeer, unsigned char worldId, MafiaNet::Time curTime) override;

        uint32_t TransformSendIntervalMs(const NetworkEntity *entity) const;

        // Sends what the pass queued. Called once per replication pass, after every entity.
        void FlushTransforms(MafiaNet::RakPeerInterface *rakPeer);

        // Recompute this viewer's interest on its next query instead of on its turn: for a change
        // that concerns this connection alone, such as gaining or losing ownership of an entity.
        void InvalidateInterest() {
            _interestStale = true;
        }

        // Whether this viewer's interest set, as of its last query, holds the entity. What
        // ReplicaManager3 constructs follows it one pass later.
        bool IsRelevant(const NetworkEntity *entity) const;

        // --- Driven by ReplicationManager ---

        // Whether this connection needs its interest recomputed before its next query regardless of
        // its turn: never computed, invalidated, or its viewer or dimension changed.
        bool InterestUrgent() const;

        // Computes this viewer's interest set into the pending result QueryReplicaList applies next.
        // Reads only shared state; runs on any thread, several connections at once.
        void ComputeInterest(InterestGrid::Scratch &scratch);

        // The entity in this slot is gone: drop it from the set and any pending result.
        void ForgetSlot(uint32_t slot);

      private:
        struct SlotRef {
            uint32_t slot;
            uint32_t generation;
        };
        struct SendStamp {
            uint32_t generation;
            MafiaNet::Time time;
        };

        NetworkEntity *Viewer() const;
        void ApplyRefresh(DataStructures::List<MafiaNet::Replica3 *> &create, DataStructures::List<MafiaNet::Replica3 *> &destroy);
        // Brings what ReplicaManager3 has constructed here back in line with the current set.
        void Resync(DataStructures::List<MafiaNet::Replica3 *> &create, DataStructures::List<MafiaNet::Replica3 *> &destroy);
        // False when a verdict needs the whole set recomputed. `admitOnly` right after a recompute:
        // what it decided stands, and only entities filed too late for it to find are let in.
        bool ApplyReevaluations(NetworkEntity *viewer, DataStructures::List<MafiaNet::Replica3 *> &create, bool admitOnly);
        void Admit(NetworkEntity *entity, uint32_t slot, DataStructures::List<MafiaNet::Replica3 *> &create);
        void SendPacket(MafiaNet::RakPeerInterface *rakPeer, MafiaNet::BitStream &packet, const MafiaNet::PRO &parameters) const;

        ReplicationManager *_manager = nullptr;
        bool _isServer               = false;
        TransformBatch _transformBatch;
        // GetMTUSize scans every peer slot, so the budget is read once per pass; 0 = not read yet.
        std::size_t _packetBudget      = 0;
        MafiaNet::PeerGuid _viewerGUID = MafiaNet::UNASSIGNED_PEER_GUID;

        // Last transform send per entity slot; a stamp left by the slot's previous occupant reads as
        // never sent.
        std::vector<SendStamp> _lastTransformSend;

        // The current set; also the hysteresis input of the next computation.
        SlotSet _relevant;
        // A computed set waiting for QueryReplicaList. Slots carry their generation, so an entity that
        // left and had its slot reused in between is not mistaken for the new occupant.
        std::vector<uint32_t> _computedSlots;
        std::vector<SlotRef> _computed;
        bool _computedReady = false;
        // What the pending result was computed for; a change since makes it stale.
        const NetworkEntity *_computedViewer = nullptr;
        MafiaNet::VirtualWorldId _computedWorld {};
        // Everything the manager queued for re-evaluation up to this sequence number has been seen.
        uint64_t _reevaluationsSeen = 0;

        NetworkEntity *_relevantViewer = nullptr;
        bool _interestStale            = false;
        bool _relevantValid            = false;
    };
} // namespace Framework::Networking::Replication
