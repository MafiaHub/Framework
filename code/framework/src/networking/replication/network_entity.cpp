/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "network_entity.h"

#include "replication_manager.h"

#include <mafianet/GetTime.h>

namespace Framework::Networking::Replication {
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
        if (!IsServerPeer()) {
            ownerGUID = incomingOwner;
        }
    }

    void NetworkEntity::WriteAllocationID(MafiaNet::Connection_RM3 *, MafiaNet::BitStream *allocationIdBitstream) const {
        allocationIdBitstream->Write(typeId);
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
        fields.Field(position);
        fields.Field(velocity);
        fields.Field(rotation);
    }

    void NetworkEntity::SerializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *) {
        constructionBitstream->Write(stateEpoch);
        FieldSerializer seed(constructionBitstream, true);
        SerializeBaseFields(seed);
        OnSerializeConstruction(seed);
        SerializeFields(seed);
    }

    bool NetworkEntity::DeserializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *) {
        uint8_t incomingEpoch = stateEpoch;
        constructionBitstream->Read(incomingEpoch);
        ApplyIncomingEpoch(incomingEpoch);
        FieldSerializer seed(constructionBitstream, false);
        SerializeBaseFields(seed);
        OnSerializeConstruction(seed);
        SerializeFields(seed);
        OnConstructed();
        return true;
    }

    void NetworkEntity::SerializeDestruction(MafiaNet::BitStream *, MafiaNet::Connection_RM3 *) {}

    bool NetworkEntity::DeserializeDestruction(MafiaNet::BitStream *, MafiaNet::Connection_RM3 *) {
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

        // The epoch rides raw ahead of the delta block: it must be present in every update, and a VDS
        // variable is omitted when unchanged, which would let a pre-override packet pass the
        // staleness check by absence.
        serializeParameters->outputBitstream[0].Write(stateEpoch);

        MafiaNet::VariableDeltaSerializer::SerializationContext ctx;
        // whenLastSerialized == 0 means this is the first send to a fresh system: write every
        // variable in full; otherwise only changed variables are written.
        _vds.BeginIdenticalSerialize(&ctx, serializeParameters->whenLastSerialized == 0, &serializeParameters->outputBitstream[0]);
        FieldSerializer fields(&_vds, &ctx);
        SerializeBaseFields(fields);
        SerializeFields(fields);
        _vds.EndSerialize(&ctx);

        // BeginIdenticalSerialize already produces one delta bitstream shared across all recipients
        // (the state is identical for every viewer), so pair it with the broadcast-identical result:
        // ReplicaManager3 serializes once per tick and reuses those bytes for every connection,
        // suppressing the send when nothing changed. Per-connection filtering (owner exclusion) still
        // happens upstream in QuerySerializationWithinWorld.
        return MafiaNet::RM3SR_BROADCAST_IDENTICALLY;
    }

    void NetworkEntity::Deserialize(MafiaNet::DeserializeParameters *deserializeParameters) {
        // Server authority gate: only accept state from the entity's current owner, rejecting a
        // stale owner whose in-flight packets land after an ownership handover.
        if (IsServerPeer() && deserializeParameters->sourceConnection && MafiaNet::ToPeerGuid(deserializeParameters->sourceConnection->GetRakNetGUID()) != ownerGUID) {
            return;
        }

        uint8_t incomingEpoch = stateEpoch;
        deserializeParameters->serializationBitstream[0].Read(incomingEpoch);
        if (!ApplyIncomingEpoch(incomingEpoch)) {
            // Stale epoch: discard this update without applying it.
            return;
        }

        MafiaNet::VariableDeltaSerializer::DeserializationContext ctx;
        _vds.BeginDeserialize(&ctx, &deserializeParameters->serializationBitstream[0]);
        FieldSerializer fields(&_vds, &ctx);
        SerializeBaseFields(fields);
        SerializeFields(fields);
        _vds.EndDeserialize(&ctx);

        // Already shifted to our local clock by RakPeer; do not subtract GetClockDifferential.
        if (deserializeParameters->timeStamp != 0) {
            lastUpdateTime = deserializeParameters->timeStamp;
        }
    }

    MafiaNet::Time NetworkEntity::GetUpdateAge() const {
        if (lastUpdateTime == 0) {
            return 0;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();
        return now > lastUpdateTime ? now - lastUpdateTime : 0;
    }

    glm::vec3 NetworkEntity::GetExtrapolatedPosition() const {
        return position + velocity * (static_cast<float>(GetUpdateAge()) / 1000.0f);
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
