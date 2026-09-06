/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_entity.h"

#include "../channels.h"
#include "replication_manager.h"

#include <mafianet/GetTime.h>
#include <utils/time.h>

#include <cstring>

namespace Framework::Networking::Replication {
    namespace {
        // Serialize channel split: the pose rides an unreliable channel (a dropped frame is covered by
        // the next one / receiver-side interpolation), game state rides a reliable-ordered channel
        // (on-change deltas must never be lost). Both are flushed as independent RakNet messages
        // because their PacketReliability differs (see Connection_RM3::SendSerialize).
        constexpr int kTransformChannel = 0;
        constexpr int kStateChannel     = 1;

        bool SameBytes(const MafiaNet::BitStream *last, const MafiaNet::BitStream &current) {
            if (!last || last->GetNumberOfBitsUsed() != current.GetNumberOfBitsUsed()) {
                return false;
            }
            const MafiaNet::BitSize_t bytes = current.GetNumberOfBytesUsed();
            return bytes == 0 || std::memcmp(last->GetData(), current.GetData(), bytes) == 0;
        }
    } // namespace

    ReplicationManager *NetworkEntity::Manager() {
        return static_cast<ReplicationManager *>(replicaManager);
    }

    const ReplicationManager *NetworkEntity::Manager() const {
        return static_cast<const ReplicationManager *>(replicaManager);
    }

    bool NetworkEntity::IsServerPeer() const {
        const auto *manager = Manager();
        return manager && manager->IsServer();
    }

    MafiaNet::PeerGuid NetworkEntity::MyGUID() const {
        const auto *manager = Manager();
        return manager ? manager->GetMyGUID() : MafiaNet::UNASSIGNED_PEER_GUID;
    }

    void NetworkEntity::AdoptIncomingOwner(MafiaNet::PeerGuid incomingOwner) {
        // The server keeps its own authoritative owner assignment and must not let an owning client
        // dictate it back; clients adopt whatever the server sends.
        if (!IsServerPeer() && ownerGUID != incomingOwner) {
            ownerGUID = incomingOwner;
            _lastTransformTime = 0;
        }
    }

    void NetworkEntity::WriteAllocationID(MafiaNet::Connection_RM3 *, MafiaNet::BitStream *allocationIdBitstream) const {
        allocationIdBitstream->Write(_typeId);
    }

    bool NetworkEntity::ApplyIncomingEpoch(uint8_t incomingEpoch) {
        if (IsServerPeer()) {
            // The server's epoch is authoritative: a mismatch is an owner update sent before the
            // owner saw the last ForceState — stale, so it must not revert the forced state.
            return incomingEpoch == stateEpoch;
        }
        stateEpoch = incomingEpoch;
        return true;
    }

    void NetworkEntity::SerializeBaseFields(FieldSerializer &fields) {
        if (fields.Writing()) {
            fields.Field(ownerGUID);
        }
        else {
            // Read into a temporary so the server can ignore a client-supplied owner (see
            // AdoptIncomingOwner); clients adopt the owner the server sends.
            MafiaNet::PeerGuid incomingOwner = ownerGUID;
            fields.Field(incomingOwner);
            AdoptIncomingOwner(incomingOwner);
        }
    }

    void NetworkEntity::SerializeTransform(FieldSerializer &fields) {
        fields.Field(position);
        fields.Field(velocity);
        fields.Field(rotation);
    }

    void NetworkEntity::SerializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *) {
        constructionBitstream->Write(stateEpoch);
        FieldSerializer seed(constructionBitstream, true);
        SerializeBaseFields(seed);
        SerializeTransform(seed);
        OnSerializeConstruction(seed);
        SerializeFields(seed);
    }

    bool NetworkEntity::DeserializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *) {
        uint8_t incomingEpoch = stateEpoch;
        constructionBitstream->Read(incomingEpoch);
        ApplyIncomingEpoch(incomingEpoch);
        FieldSerializer seed(constructionBitstream, false);
        SerializeBaseFields(seed);
        SerializeTransform(seed);
        OnSerializeConstruction(seed);
        SerializeFields(seed);
        OnConstructed();
        return true;
    }

    void NetworkEntity::SerializeDestruction(MafiaNet::BitStream *, MafiaNet::Connection_RM3 *) {}

    bool NetworkEntity::DeserializeDestruction(MafiaNet::BitStream *, MafiaNet::Connection_RM3 *sourceConnection) {
        // Server authority gate (mirrors Deserialize): the base ReplicaManager3 destruction dispatch
        // resolves the target by a NetworkID taken straight from the packet body and deletes it the
        // moment this returns true, with no ownership check of its own. Without this gate any client
        // could despawn arbitrary entities — other players' avatars, server-owned objects — and, via
        // QueryRelayDestruction, have the server relay that deletion to everyone. Only honour a
        // destruction from the entity's current owner; fail closed on a missing connection. Returning
        // false keeps the entity alive. Clients still accept the server's authoritative destructions.
        if (IsServerPeer() && (!sourceConnection || MafiaNet::ToPeerGuid(sourceConnection->GetRakNetGUID()) != ownerGUID)) {
            return false;
        }
        return true;
    }

    void NetworkEntity::DeallocReplica(MafiaNet::Connection_RM3 *) {
        delete this;
    }

    void NetworkEntity::SerializeForcedState(FieldSerializer &fields) {
        fields.Field(position);
        fields.Field(rotation);
    }

    void NetworkEntity::ForceState() {
        if (auto *manager = Manager()) {
            manager->ForceState(this);
        }
    }

    void NetworkEntity::SetOwner(MafiaNet::PeerGuid guid) {
        if (auto *manager = Manager()) {
            manager->SetOwner(this, guid);
        }
        else {
            ownerGUID = guid;
        }
    }

    bool NetworkEntity::IsOwner() const {
        if (ownerGUID == MyGUID()) {
            return true;
        }
        // The server holds authority over entities left unowned (server-owned).
        return IsServerPeer() && ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID;
    }

    // --- Per-tick delta serialization (VariableDeltaSerializer) ---

    void NetworkEntity::OnUserReplicaPreSerializeTick() {
        // Reset the per-tick "already compared" flag so identical-broadcast caching works (see
        // VariableDeltaSerializer::OnPreSerializeTick). Called once per replica per serialize tick.
        _vds.OnPreSerializeTick();
    }

    MafiaNet::RM3SerializationResult NetworkEntity::Serialize(MafiaNet::SerializeParameters *serializeParameters) {
        serializeParameters->messageTimestamp = MafiaNet::GetTime();

        // The epoch rides raw at the head of each channel: it must be present in every update (a VDS
        // variable is omitted when unchanged, which would let a pre-override packet pass the staleness
        // check by absence), and the two channels are independent messages, so each carries its own.

        // Channel 0 — transform: raw pose, ordered per entity by the receiver's timestamp gate. Written
        // every tick; ReplicaManager3 memcmp-dedupes it against the last broadcast.
        serializeParameters->outputBitstream[kTransformChannel].Write(stateEpoch);
        FieldSerializer transform(&serializeParameters->outputBitstream[kTransformChannel], true);
        SerializeTransform(transform);
        serializeParameters->pro[kTransformChannel].reliability     = MafiaNet::Reliability::Unreliable;
        serializeParameters->pro[kTransformChannel].orderingChannel = ToOrderingChannel(Channel::Transform);

        // Idle refresh (see kTransformRefreshMs). An unchanged state channel is empty, so a forced send
        // carries only the pose.
        const MafiaNet::Time now                = serializeParameters->curTime;
        const MafiaNet::BitStream *lastTransform = serializeParameters->lastSentBitstream[kTransformChannel];
        bool refreshTransform                   = false;
        if (!SameBytes(lastTransform, serializeParameters->outputBitstream[kTransformChannel])) {
            if (lastTransform && lastTransform->GetNumberOfBitsUsed() > 0) {
                _transformMoved      = true;
                _lastTransformChange = now;
            }
            _lastTransformSend = now;
        }
        else if (_transformMoved && now >= _lastTransformSend) {
            const bool inBurst          = now >= _lastTransformChange && now - _lastTransformChange < kTransformRefreshBurstMs;
            const MafiaNet::Time period = inBurst ? kTransformRefreshMs : kTransformHeartbeatMs;
            if (now - _lastTransformSend >= period) {
                _lastTransformSend = now;
                refreshTransform   = true;
            }
        }

        // Channel 1 — state: owner + game fields, VDS delta, reliable-ordered. whenLastSerialized == 0
        // means first send to a fresh system: write every variable in full; else only changed ones.
        serializeParameters->outputBitstream[kStateChannel].Write(stateEpoch);
        MafiaNet::VariableDeltaSerializer::SerializationContext ctx;
        _vds.BeginIdenticalSerialize(&ctx, serializeParameters->whenLastSerialized == 0, &serializeParameters->outputBitstream[kStateChannel]);
        FieldSerializer fields(&_vds, &ctx);
        SerializeBaseFields(fields);
        SerializeFields(fields);
        _vds.EndSerialize(&ctx);
        if (!ctx.anyVariablesWritten) {
            // Nothing to deliver, so no message: the epoch prefix alone would be sent reliably on
            // every forced refresh.
            serializeParameters->outputBitstream[kStateChannel].Reset();
        }
        serializeParameters->pro[kStateChannel].reliability    = MafiaNet::Reliability::ReliableOrdered;
        serializeParameters->pro[kStateChannel].orderingChannel = ToOrderingChannel(Channel::State);

        // Both channels carry recipient-identical bytes, so broadcast-identically: ReplicaManager3
        // serializes once per tick, reuses the bytes for every connection, and suppresses each channel
        // whose bytes are unchanged. Per-connection filtering (owner exclusion) still happens upstream
        // in QuerySerializationWithinWorld.
        return refreshTransform ? MafiaNet::RM3SR_BROADCAST_IDENTICALLY_FORCE_SERIALIZATION : MafiaNet::RM3SR_BROADCAST_IDENTICALLY;
    }

    void NetworkEntity::Deserialize(MafiaNet::DeserializeParameters *deserializeParameters) {
        // Server authority gate (applies to both channels): only accept updates from the entity's
        // current owner, rejecting a stale owner whose in-flight packets land after a handover.
        if (IsServerPeer() && deserializeParameters->sourceConnection && MafiaNet::ToPeerGuid(deserializeParameters->sourceConnection->GetRakNetGUID()) != ownerGUID) {
            return;
        }

        bool transformUpdated = false;

        // Channel 0 — transform. A pose older than the newest applied one is dropped. The epoch fences
        // a forced-state override against an owner's in-flight pose (which would otherwise revert it);
        // the server drops a stale-epoch pose, clients adopt.
        const MafiaNet::Time sentAt = deserializeParameters->timeStamp;
        if (deserializeParameters->bitstreamWrittenTo[kTransformChannel] && (sentAt == 0 || _lastTransformTime == 0 || sentAt >= _lastTransformTime)) {
            uint8_t incomingEpoch = stateEpoch;
            deserializeParameters->serializationBitstream[kTransformChannel].Read(incomingEpoch);
            if (ApplyIncomingEpoch(incomingEpoch)) {
                FieldSerializer transform(&deserializeParameters->serializationBitstream[kTransformChannel], false);
                SerializeTransform(transform);
                transformUpdated = true;
                if (sentAt != 0) {
                    _lastTransformTime = sentAt;
                }
            }
        }

        // Channel 1 — state.
        if (deserializeParameters->bitstreamWrittenTo[kStateChannel]) {
            uint8_t incomingEpoch = stateEpoch;
            deserializeParameters->serializationBitstream[kStateChannel].Read(incomingEpoch);
            if (ApplyIncomingEpoch(incomingEpoch)) {
                MafiaNet::VariableDeltaSerializer::DeserializationContext ctx;
                _vds.BeginDeserialize(&ctx, &deserializeParameters->serializationBitstream[kStateChannel]);
                FieldSerializer fields(&_vds, &ctx);
                SerializeBaseFields(fields);
                SerializeFields(fields);
                _vds.EndDeserialize(&ctx);
            }
        }

        // Already shifted to our local clock by RakPeer; do not subtract GetClockDifferential.
        if (deserializeParameters->timeStamp > lastUpdateTime) {
            lastUpdateTime = deserializeParameters->timeStamp;
        }

        OnDeserialized(transformUpdated);
    }

    MafiaNet::Time NetworkEntity::GetUpdateAge() const {
        if (lastUpdateTime == 0) {
            return 0;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();
        return now > lastUpdateTime ? now - lastUpdateTime : 0;
    }

    glm::vec3 NetworkEntity::GetExtrapolatedPosition() const {
        return position + velocity * Framework::Utils::Time::MsToSeconds(static_cast<float>(GetUpdateAge()));
    }

    NetworkEntity *NetworkEntity::ResolveSibling(MafiaNet::NetworkID networkId) const {
        const auto *manager = Manager();
        return manager ? manager->GetEntityByNetworkID(networkId) : nullptr;
    }

    MafiaNet::RM3ConstructionState NetworkEntity::QueryConstructionWithinWorld(MafiaNet::Connection_RM3 *destinationConnection, MafiaNet::ReplicaManager3 *) {
        return QueryConstruction_ServerConstruction(destinationConnection, IsServerPeer());
    }

    bool NetworkEntity::QueryRemoteConstruction(MafiaNet::Connection_RM3 *sourceConnection) {
        return QueryRemoteConstruction_ServerConstruction(sourceConnection, IsServerPeer());
    }

    MafiaNet::RM3QuerySerializationResult NetworkEntity::QuerySerializationWithinWorld(MafiaNet::Connection_RM3 *destinationConnection) {
        if (IsServerPeer()) {
            // Relay to everyone except the authoritative owner (no echo back to it).
            if (MafiaNet::ToPeerGuid(destinationConnection->GetRakNetGUID()) == ownerGUID) {
                return MafiaNet::RM3QSR_DO_NOT_CALL_SERIALIZE;
            }
            return MafiaNet::RM3QSR_CALL_SERIALIZE;
        }

        // Client: only push upstream for entities we currently own.
        return ownerGUID == MyGUID() ? MafiaNet::RM3QSR_CALL_SERIALIZE : MafiaNet::RM3QSR_DO_NOT_CALL_SERIALIZE;
    }

    MafiaNet::RM3ActionOnPopConnection NetworkEntity::QueryActionOnPopConnection(MafiaNet::Connection_RM3 *droppedConnection) const {
        return IsServerPeer() ? QueryActionOnPopConnection_Server(droppedConnection) : QueryActionOnPopConnection_Client(droppedConnection);
    }
} // namespace Framework::Networking::Replication
