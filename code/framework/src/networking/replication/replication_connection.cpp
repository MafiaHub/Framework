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

#include <mafianet/GetTime.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

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

    void ReplicationConnection::QueryReplicaList(DataStructures::List<MafiaNet::Replica3 *> &newReplicasToCreate, DataStructures::List<MafiaNet::Replica3 *> &existingReplicasToDestroy) {
        // Only the server decides what exists on a remote system.
        if (!_isServer || !_manager) {
            return;
        }

        const auto viewerGUID = MafiaNet::ToPeerGuid(GetRakNetGUID());
        NetworkEntity *viewer = _manager->GetViewer(viewerGUID);
        if (!viewer) {
            // Connection not yet associated with a controlled entity (still handshaking).
            _relevantValid = false;
            return;
        }

        // Dimension changes, ownership changes and lifecycle invalidation bypass the phase delay.
        const bool worldChanged = GetVirtualWorld() != viewer->GetVirtualWorld();
        SetVirtualWorld(viewer->GetVirtualWorld());
        const uint32_t generation       = _manager->InterestGeneration();
        const uint32_t urgentGeneration = _manager->InterestUrgentGeneration();
        const bool urgent               = !_relevantValid || _interestStale || _relevantViewer != viewer || worldChanged || _relevantUrgentGeneration != urgentGeneration;
        if (_interestRefresh.Due(MafiaNet::GetTime(), _manager->InterestRefreshInterval(), GetRakNetGUID().g, urgent, _relevantGeneration != generation)) {
            _previousRelevant.swap(_relevant);
            _relevant.clear();
            _manager->CollectInterest(viewer, viewerGUID, _previousRelevant, _relevant);
            _relevantGeneration       = generation;
            _relevantUrgentGeneration = urgentGeneration;
            _relevantViewer           = viewer;
            _relevantValid            = true;
            _interestStale            = false;
            for (auto it = _lastTransformSend.begin(); it != _lastTransformSend.end();) {
                it = _relevant.contains(const_cast<NetworkEntity *>(it->first)) ? std::next(it) : _lastTransformSend.erase(it);
            }
        }

        for (NetworkEntity *entity : _relevant) {
            if (!HasReplicaConstructed(entity)) {
                newReplicasToCreate.Push(entity, _FILE_AND_LINE_);
            }
        }

        DataStructures::List<MafiaNet::Replica3 *> constructed;
        GetConstructedReplicas(constructed);
        for (unsigned i = 0; i < constructed.Size(); ++i) {
            auto *entity = static_cast<NetworkEntity *>(constructed[i]);
            if (entity && !_relevant.contains(entity)) {
                existingReplicasToDestroy.Push(entity, _FILE_AND_LINE_);
            }
        }
    }

    uint32_t ReplicationConnection::TransformSendIntervalMs(const NetworkEntity *entity) const {
        if (!_isServer || !_manager || !entity || !_relevantValid || !_relevantViewer || !_manager->HasSerializeRateBands()) {
            return 0;
        }
        const NetworkEntity *viewer = _relevantViewer;
        if (entity == viewer || entity->ownerGUID == _viewerGUID || entity->streaming.alwaysVisible || entity->streaming.targetGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
            return 0;
        }
        const SerializeRateBands &bands = _manager->GetSerializeRateBands(entity->GetTypeId());
        if (bands.midIntervalMs == 0 && bands.farIntervalMs == 0) {
            return 0;
        }
        const glm::vec3 delta = entity->position - viewer->position;
        return ReplicationManager::TransformSendIntervalMs(bands, glm::dot(delta, delta));
    }

    MafiaNet::SendSerializeIfChangedResult ReplicationConnection::SendSerialize(MafiaNet::Replica3 *replica, bool indicesToSend[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::BitStream serializationData[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS],
        MafiaNet::Time timestamp, MafiaNet::PRO sendParameters[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS], MafiaNet::RakPeerInterface *rakPeer, unsigned char worldId, MafiaNet::Time curTime) {
        // indicesToSend may be the replica's shared broadcast record, so the selection is edited on a copy.
        bool selected[MafiaNet::RM3_NUM_OUTPUT_BITSTREAM_CHANNELS];
        std::memcpy(selected, indicesToSend, sizeof(selected));
        MafiaNet::BitStream &transform = serializationData[kTransformChannel];
        if (selected[kTransformChannel] && transform.GetNumberOfBitsUsed() != 0) {
            const auto *entity      = static_cast<const NetworkEntity *>(replica);
            const uint32_t interval = TransformSendIntervalMs(entity);
            if (interval > 0) {
                const auto it = _lastTransformSend.find(entity);
                if (it != _lastTransformSend.end() && curTime >= it->second && curTime - it->second < interval) {
                    selected[kTransformChannel] = false;
                }
                else {
                    _lastTransformSend[entity] = curTime;
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
