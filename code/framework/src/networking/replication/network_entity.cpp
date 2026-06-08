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

    PeerGuid NetworkEntity::MyGUID() const {
        const auto *manager = Manager();
        return manager ? manager->GetMyGUID() : UnassignedPeer();
    }

    void NetworkEntity::AdoptIncomingOwner(PeerGuid incomingOwner) {
        // The server keeps its own authoritative owner assignment and must not let an owning client
        // dictate it back; clients adopt whatever the server sends.
        if (!IsServerPeer()) {
            ownerGUID = incomingOwner;
        }
    }

    void NetworkEntity::WriteAllocationID(MafiaNet::Connection_RM3 *, MafiaNet::BitStream *allocationIdBitstream) const {
        allocationIdBitstream->Write(typeId);
    }

    void NetworkEntity::SerializeBaseState(MafiaNet::BitStream *bs, bool write) {
        if (write) {
            bs->Write(ownerGUID);
        }
        else {
            PeerGuid incomingOwner = ownerGUID;
            bs->Read(incomingOwner);
            AdoptIncomingOwner(incomingOwner);
        }
        bs->Serialize(write, position);
        bs->Serialize(write, velocity);
        bs->Serialize(write, rotation);
    }

    void NetworkEntity::SerializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *) {
        SerializeBaseState(constructionBitstream, true);
        OnSerializeConstruction(constructionBitstream, true);
    }

    bool NetworkEntity::DeserializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *) {
        SerializeBaseState(constructionBitstream, false);
        OnSerializeConstruction(constructionBitstream, false);
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

    void NetworkEntity::SerializeForcedState(MafiaNet::BitStream *bs, bool write) {
        bs->Serialize(write, position);
        bs->Serialize(write, rotation);
    }

    void NetworkEntity::ForceState() {
        if (auto *manager = Manager()) {
            manager->ForceState(this);
        }
    }

    void NetworkEntity::SetOwner(PeerGuid guid) {
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
        return IsServerPeer() && !IsAssigned(ownerGUID);
    }

    // --- Per-tick delta serialization (VariableDeltaSerializer) ---

    void NetworkEntity::OnUserReplicaPreSerializeTick() {
        // Reset the per-tick "already compared" flag so identical-broadcast caching works (see
        // VariableDeltaSerializer::OnPreSerializeTick). Called once per replica per serialize tick.
        _vds.OnPreSerializeTick();
    }

    MafiaNet::RM3SerializationResult NetworkEntity::Serialize(MafiaNet::SerializeParameters *serializeParameters) {
        serializeParameters->messageTimestamp = MafiaNet::GetTime();

        MafiaNet::VariableDeltaSerializer::SerializationContext ctx;
        // whenLastSerialized == 0 means this is the first send to a fresh system: write every
        // variable in full; otherwise only changed variables are written.
        _vds.BeginIdenticalSerialize(&ctx, serializeParameters->whenLastSerialized == 0, &serializeParameters->outputBitstream[0]);
        // ownerGUID stays explicit; the receiver filters it via AdoptIncomingOwner.
        _vds.SerializeVariable(&ctx, ownerGUID);
        FieldSerializer fields(&_vds, &ctx);
        fields.Field(position);
        fields.Field(velocity);
        fields.Field(rotation);
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
        if (IsServerPeer() && deserializeParameters->sourceConnection && ToPeerGuid(deserializeParameters->sourceConnection->GetRakNetGUID()) != ownerGUID) {
            return;
        }

        MafiaNet::VariableDeltaSerializer::DeserializationContext ctx;
        _vds.BeginDeserialize(&ctx, &deserializeParameters->serializationBitstream[0]);
        // Read into a temporary so the server can ignore a client-supplied owner (see
        // AdoptIncomingOwner); clients adopt the owner the server sends.
        PeerGuid incomingOwner = ownerGUID;
        _vds.DeserializeVariable(&ctx, incomingOwner);
        AdoptIncomingOwner(incomingOwner);
        FieldSerializer fields(&_vds, &ctx);
        fields.Field(position);
        fields.Field(velocity);
        fields.Field(rotation);
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
            if (ToPeerGuid(destinationConnection->GetRakNetGUID()) == ownerGUID) {
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
