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

#include <mafianet/get_time.h>
#include <utils/profiler.h>

#include <algorithm>
#include <cstring>

namespace Framework::Networking::Replication {
    namespace {
        constexpr int kTransformChannel = 0;
    } // namespace

    ReplicationConnection::ReplicationConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID guid, ReplicationManager *manager, bool isServer)
        : Connection_RM3(systemAddress, guid)
        , _manager(manager)
        , _isServer(isServer)
        , _viewerGUID(MafiaNet::ToPeerGuid(guid)) {}

    MafiaNet::Replica3 *ReplicationConnection::AllocReplica(MafiaNet::BitStream *allocationIdBitstream, MafiaNet::ReplicaManager3 *) {
        uint32_t typeId = 0;
        allocationIdBitstream->Read(typeId);
        // The instance's state is populated by DeserializeConstruction (called immediately after);
        // any backing game object is requested from NetworkEntity::OnConstructed.
        return EntityRegistry::Get().Create(typeId);
    }

    NetworkEntity *ReplicationConnection::Viewer() const {
        return _manager ? _manager->GetViewer(_viewerGUID) : nullptr;
    }

    bool ReplicationConnection::IsRelevant(const NetworkEntity *entity) const {
        if (!_manager || !entity) {
            return false;
        }
        const uint32_t slot = _manager->Interest().SlotOf(entity);
        return slot != InterestGrid::kNoSlot && _relevant.Test(slot);
    }

    bool ReplicationConnection::InterestUrgent() const {
        const NetworkEntity *viewer = Viewer();
        if (!viewer) {
            return false;
        }
        // Dimension changes, ownership changes and lifecycle invalidation bypass the turn; so does
        // having missed re-evaluations the manager has since discarded.
        return !_relevantValid || _interestStale || _relevantViewer != viewer || GetVirtualWorld() != viewer->GetVirtualWorld() || _reevaluationsSeen < _manager->ReevaluationBase();
    }

    void ReplicationConnection::ComputeInterest(InterestGrid::Scratch &scratch) {
        const NetworkEntity *viewer = Viewer();
        if (!viewer) {
            return;
        }
        const InterestGrid &interest = _manager->Interest();
        _interestStale               = false;
        _computedViewer              = viewer;
        _computedWorld               = viewer->GetVirtualWorld();
        interest.CollectVisible(viewer, _viewerGUID, _relevant, scratch, _computedSlots);
        _computed.clear();
        for (const uint32_t slot : _computedSlots) {
            _computed.push_back({slot, interest.GenerationOf(slot)});
        }
        _computedReady = true;
        PrepareDiff();
    }

    void ReplicationConnection::PrepareDiff() {
        const InterestGrid &interest = _manager->Interest();
        _computedSet.Clear();
        _pendingCreate.clear();
        _pendingDestroy.clear();
        for (const uint32_t slot : _computedSlots) {
            NetworkEntity *entity = interest.EntityAt(slot);
            if (entity == nullptr) {
                continue;
            }
            _computedSet.Set(slot);
            if (!HasReplicaConstructed(entity)) {
                _pendingCreate.push_back(entity);
            }
        }
        for (unsigned i = 0; i < constructedReplicaList.Size(); ++i) {
            auto *entity        = static_cast<NetworkEntity *>(constructedReplicaList[i]->replica);
            const uint32_t slot = interest.SlotOf(entity);
            if (slot == InterestGrid::kNoSlot || !_computedSet.Test(slot)) {
                _pendingDestroy.push_back(entity);
            }
        }
        _diffMembership  = _manager->GetMembershipGeneration();
        _diffConstructed = constructedReplicaList.Size();
        _diffReady       = true;
    }

    bool ReplicationConnection::DiffStillHolds() const {
        // A replica dereferenced since may be one the lists point at, and its slot may have a new
        // occupant; a constructed list of another size is not the one the lists were taken against.
        return _diffReady && _diffMembership == _manager->GetMembershipGeneration() && _diffConstructed == constructedReplicaList.Size();
    }

    void ReplicationConnection::ForgetSlot(uint32_t slot) {
        _relevant.Reset(slot);
        // A pending result naming the slot is caught by its generation when applied.
    }

    void ReplicationConnection::QueryReplicaList(DataStructures::List<MafiaNet::Replica3 *> &newReplicasToCreate, DataStructures::List<MafiaNet::Replica3 *> &existingReplicasToDestroy) {
        // Only the server decides what exists on a remote system.
        if (!_isServer || !_manager) {
            return;
        }
        // What other threads queued since the pass began concerns this connection as much.
        _manager->ApplyPending();

        NetworkEntity *viewer = Viewer();
        if (!viewer) {
            // Connection not yet associated with a controlled entity (still handshaking).
            _relevantValid = false;
            _computedReady = false;
            return;
        }

        // A result computed earlier this tick is stale if what it was computed for changed since.
        const bool recompute = _computedReady ? (_interestStale || _computedViewer != viewer || _computedWorld != viewer->GetVirtualWorld()) : InterestUrgent();
        SetVirtualWorld(viewer->GetVirtualWorld());
        if (recompute) {
            FW_PROFILE_SCOPE_N("Replication::CollectInterest");
            ComputeInterest(_manager->MainScratch());
        }
        // ReplicaManager3 changed what it holds here outside this diff, as when it drops a replica a
        // client failed to destroy: put it right now rather than on the viewer's next turn.
        if (!_computedReady && _relevantValid && constructedReplicaList.Size() != _relevant.Count()) {
            Resync(newReplicasToCreate, existingReplicasToDestroy);
        }
        if (!_computedReady && !ApplyReevaluations(viewer, newReplicasToCreate, false)) {
            FW_PROFILE_SCOPE_N("Replication::CollectInterest");
            newReplicasToCreate.Clear(true, _FILE_AND_LINE_);
            ComputeInterest(_manager->MainScratch());
        }
        if (_computedReady) {
            FW_PROFILE_SCOPE_N("Replication::ApplyInterest");
            ApplyRefresh(newReplicasToCreate, existingReplicasToDestroy);
            // An entity created since the spatial index last saw it is still filed where it was made.
            ApplyReevaluations(viewer, newReplicasToCreate, true);
            _relevantViewer = viewer;
            _relevantValid  = true;
        }
        _reevaluationsSeen = _manager->ReevaluationEnd();
    }

    void ReplicationConnection::Admit(NetworkEntity *entity, uint32_t slot, DataStructures::List<MafiaNet::Replica3 *> &create) {
        _relevant.Set(slot);
        if (!HasReplicaConstructed(entity)) {
            create.Push(entity, _FILE_AND_LINE_);
        }
    }

    void ReplicationConnection::ApplyRefresh(DataStructures::List<MafiaNet::Replica3 *> &create, DataStructures::List<MafiaNet::Replica3 *> &destroy) {
        if (DiffStillHolds()) {
            _relevant.Swap(_computedSet);
            for (NetworkEntity *entity : _pendingCreate) {
                create.Push(entity, _FILE_AND_LINE_);
            }
            for (NetworkEntity *entity : _pendingDestroy) {
                destroy.Push(entity, _FILE_AND_LINE_);
            }
            _computed.clear();
            _computedReady = false;
            _diffReady     = false;
            _manager->NotePreparedDiff();
            return;
        }
        _diffReady                   = false;
        const InterestGrid &interest = _manager->Interest();
        _relevant.Clear();
        for (const SlotRef &ref : _computed) {
            NetworkEntity *entity = interest.EntityAt(ref.slot);
            if (entity != nullptr && interest.GenerationOf(ref.slot) == ref.generation) {
                Admit(entity, ref.slot, create);
            }
        }
        _computed.clear();
        _computedReady = false;

        // Against what ReplicaManager3 holds rather than the previous set: whatever went missing
        // between the two is put right here.
        for (unsigned i = 0; i < constructedReplicaList.Size(); ++i) {
            auto *entity        = static_cast<NetworkEntity *>(constructedReplicaList[i]->replica);
            const uint32_t slot = interest.SlotOf(entity);
            if (slot == InterestGrid::kNoSlot || !_relevant.Test(slot)) {
                destroy.Push(entity, _FILE_AND_LINE_);
            }
        }
    }

    void ReplicationConnection::Resync(DataStructures::List<MafiaNet::Replica3 *> &create, DataStructures::List<MafiaNet::Replica3 *> &destroy) {
        const InterestGrid &interest = _manager->Interest();
        _relevant.ForEach([&](uint32_t slot) {
            NetworkEntity *entity = interest.EntityAt(slot);
            if (entity != nullptr && !HasReplicaConstructed(entity)) {
                create.Push(entity, _FILE_AND_LINE_);
            }
        });
        for (unsigned i = 0; i < constructedReplicaList.Size(); ++i) {
            auto *entity        = static_cast<NetworkEntity *>(constructedReplicaList[i]->replica);
            const uint32_t slot = interest.SlotOf(entity);
            if (slot == InterestGrid::kNoSlot || !_relevant.Test(slot)) {
                destroy.Push(entity, _FILE_AND_LINE_);
            }
        }
    }

    bool ReplicationConnection::ApplyReevaluations(NetworkEntity *viewer, DataStructures::List<MafiaNet::Replica3 *> &create, bool admitOnly) {
        const uint64_t end = _manager->ReevaluationEnd();
        if (_reevaluationsSeen >= end) {
            return true;
        }
        const InterestGrid &interest = _manager->Interest();
        // A connection behind the base (new, or skipped by a pass) has just recomputed: what it missed
        // is in that result, and the queue no longer holds it.
        for (uint64_t i = std::max(_reevaluationsSeen, _manager->ReevaluationBase()); i < end; ++i) {
            const auto [slot, generation] = _manager->ReevaluationAt(i);
            NetworkEntity *entity         = interest.EntityAt(slot);
            if (entity == nullptr || interest.GenerationOf(slot) != generation) {
                continue;
            }
            const bool wasRelevant = _relevant.Test(slot);
            switch (interest.Evaluate(viewer, _viewerGUID, slot, wasRelevant)) {
            case InterestGrid::Verdict::In: {
                if (!wasRelevant) {
                    Admit(entity, slot, create);
                }
                // Brought in with what it cannot be shown without, also when it was shown already
                // and has just come to depend on something.
                const uint32_t dependency = interest.DependencyOf(slot);
                if (dependency != InterestGrid::kNoSlot && !_relevant.Test(dependency)) {
                    NetworkEntity *needed = interest.EntityAt(dependency);
                    if (needed->streaming.visible && needed->streaming.Admits(_viewerGUID)) {
                        Admit(needed, dependency, create);
                    }
                }
                break;
            }
            case InterestGrid::Verdict::Out:
                // Leaving is decided with the whole set in view: the entity may still be held as
                // another one's dependency.
                if (wasRelevant && !admitOnly) {
                    return false;
                }
                break;
            case InterestGrid::Verdict::Recompute:
                if (!admitOnly) {
                    return false;
                }
                break;
            }
        }
        return true;
    }

    uint32_t ReplicationConnection::TransformSendIntervalMs(const NetworkEntity *entity) const {
        if (!_isServer || !_manager || !entity || !_relevantValid || !_relevantViewer || !_manager->HasSerializeRateBands()) {
            return 0;
        }
        // Asked on the pass's workers: of what the pass took of both entities under their locks.
        const NetworkEntity *viewer = _relevantViewer;
        if (entity == viewer || entity->_pass.owner == _viewerGUID || entity->_pass.unthrottled) {
            return 0;
        }
        const SerializeRateBands &bands = _manager->GetSerializeRateBands(entity->GetTypeId());
        if (bands.nearIntervalMs == 0 && bands.midIntervalMs == 0 && bands.farIntervalMs == 0) {
            return 0;
        }
        const glm::vec3 delta = entity->_pass.position - viewer->_pass.position;
        return ReplicationManager::TransformSendIntervalMs(bands, glm::dot(delta, delta));
    }

    MafiaNet::SendSerializeIfChangedResult ReplicationConnection::SendSerialize(MafiaNet::Replica3 *replica, bool indicesToSend[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::BitStream serializationData[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS],
        MafiaNet::Time timestamp, MafiaNet::PRO sendParameters[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::RakPeerInterface *rakPeer, unsigned char worldId, MafiaNet::Time curTime) {
        // indicesToSend may be the replica's shared broadcast record, so the selection is edited on a copy.
        bool selected[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS];
        std::memcpy(selected, indicesToSend, sizeof(selected));
        MafiaNet::BitStream &transform = serializationData[kTransformChannel];
        if (selected[kTransformChannel] && transform.GetNumberOfBitsUsed() != 0) {
            const auto *entity           = static_cast<const NetworkEntity *>(replica);
            const uint32_t interval      = TransformSendIntervalMs(entity);
            const InterestGrid &interest = _manager->Interest();
            const uint32_t slot          = interest.SlotOf(entity);
            if (interval > 0 && slot != InterestGrid::kNoSlot) {
                if (_lastTransformSend.size() < interest.Capacity()) {
                    _lastTransformSend.resize(interest.Capacity(), SendStamp {0, 0});
                }
                SendStamp &last           = _lastTransformSend[slot];
                const uint32_t generation = interest.GenerationOf(slot);
                if (last.generation == generation && last.time != 0 && curTime >= last.time && curTime - last.time < interval) {
                    selected[kTransformChannel] = false;
                }
                else {
                    last = {generation, curTime};
                }
            }
        }
        // RM3 reuses one SerializeParameters for the whole pass, so a cached broadcast carries the stamp
        // of whichever entity serialized last. The pass time is the same for every entity, which is
        // also what lets their poses share a batch.
        const MafiaNet::Time sentAt = timestamp != 0 ? curTime : 0;

        bool batched = false;
        if (selected[kTransformChannel] && _manager->TransformBatchingEnabled()) {
            if (_packetBudget == 0) {
                // Headroom for UDP/IP and MafiaNet's datagram and message headers: a batch is never fragmented.
                _packetBudget = static_cast<std::size_t>(std::max(0, rakPeer->GetMTUSize(GetSystemAddress()) - 128));
            }
            batched                     = _transformBatch.Queue(replica->GetNetworkID(), transform, sentAt, sendParameters[kTransformChannel], worldId, _packetBudget, [this, rakPeer](MafiaNet::BitStream &packet, const MafiaNet::PRO &parameters) {
                SendPacket(rakPeer, packet, parameters);
            });
            selected[kTransformChannel] = !batched;
        }

        const MafiaNet::SendSerializeIfChangedResult result = Connection_RM3::SendSerialize(replica, selected, serializationData, sentAt, sendParameters, rakPeer, worldId, curTime);
        return batched ? MafiaNet::SSICR_SENT_DATA : result;
    }

    void ReplicationConnection::FlushTransforms(MafiaNet::RakPeerInterface *rakPeer) {
        _transformBatch.Flush([this, rakPeer](MafiaNet::BitStream &packet, const MafiaNet::PRO &parameters) {
            SendPacket(rakPeer, packet, parameters);
        });
        _packetBudget = 0;
    }

    void ReplicationConnection::SendPacket(MafiaNet::RakPeerInterface *rakPeer, MafiaNet::BitStream &packet, const MafiaNet::PRO &parameters) const {
        rakPeer->Send(&packet, parameters.priority, parameters.reliability, parameters.orderingChannel, GetSystemAddress(), false, parameters.sendReceipt);
    }
} // namespace Framework::Networking::Replication
