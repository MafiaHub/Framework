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
#include <mafianet/string.h>

namespace Framework::Networking::Replication {
    bool NetworkEntity::IsServerPeer() const {
        const auto *manager = static_cast<const ReplicationManager *>(replicaManager);
        return manager && manager->IsServer();
    }

    uint64_t NetworkEntity::MyGUID() const {
        const auto *manager = static_cast<const ReplicationManager *>(replicaManager);
        return manager ? manager->GetMyGUID() : 0xFFFFFFFFFFFFFFFF;
    }

    void NetworkEntity::WriteAllocationID(MafiaNet::Connection_RM3 *, MafiaNet::BitStream *allocationIdBitstream) const {
        allocationIdBitstream->Write(typeId);
    }

    // --- One-shot construction snapshot (full state, no delta) ---

    void NetworkEntity::WriteConstruction(MafiaNet::BitStream *bs) const {
        bs->Write(ownerGUID);
        bs->Write(position);
        bs->Write(velocity);
        bs->Write(rotation);
        bs->Write(virtualWorld);
        bs->Write(modelHash);
        bs->Write(scale);
        bs->Write(MafiaNet::RakString(modelName.c_str()));
    }

    void NetworkEntity::ReadConstruction(MafiaNet::BitStream *bs) {
        // The server keeps its own authoritative owner assignment and must not let an owning client
        // dictate it back; clients adopt whatever the server sends.
        uint64_t incomingOwner = ownerGUID;
        bs->Read(incomingOwner);
        if (!IsServerPeer()) {
            ownerGUID = incomingOwner;
        }
        bs->Read(position);
        bs->Read(velocity);
        bs->Read(rotation);
        bs->Read(virtualWorld);
        bs->Read(modelHash);
        bs->Read(scale);
        MafiaNet::RakString name;
        bs->Read(name);
        modelName = name.C_String();
    }

    void NetworkEntity::SerializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *) {
        WriteConstruction(constructionBitstream);
        OnSerializeConstruction(constructionBitstream);
    }

    bool NetworkEntity::DeserializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *) {
        ReadConstruction(constructionBitstream);
        OnDeserializeConstruction(constructionBitstream);
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

    void NetworkEntity::WriteForcedState(MafiaNet::BitStream *bs) const {
        bs->Write(position);
        bs->Write(rotation);
    }

    void NetworkEntity::ReadForcedState(MafiaNet::BitStream *bs) {
        bs->Read(position);
        bs->Read(rotation);
    }

    void NetworkEntity::ForceState() {
        if (auto *manager = static_cast<ReplicationManager *>(replicaManager)) {
            manager->ForceState(this);
        }
    }

    void NetworkEntity::SetOwner(uint64_t guid) {
        if (auto *manager = static_cast<ReplicationManager *>(replicaManager)) {
            manager->SetOwner(this, guid);
        }
        else {
            ownerGUID = guid;
        }
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
        _vds.SerializeVariable(&ctx, ownerGUID);
        _vds.SerializeVariable(&ctx, position);
        _vds.SerializeVariable(&ctx, velocity);
        _vds.SerializeVariable(&ctx, rotation);
        SerializeFields(&_vds, &ctx);
        _vds.EndSerialize(&ctx);

        // Per-connection delta path: ReplicaManager3 compares against the last bytes sent to each
        // system and suppresses the send when nothing changed.
        return MafiaNet::RM3SR_SERIALIZED_UNIQUELY;
    }

    void NetworkEntity::Deserialize(MafiaNet::DeserializeParameters *deserializeParameters) {
        // Server authority gate: only accept state from the entity's current owner, rejecting a
        // stale owner whose in-flight packets land after an ownership handover.
        if (IsServerPeer() && deserializeParameters->sourceConnection && deserializeParameters->sourceConnection->GetRakNetGUID().g != ownerGUID) {
            return;
        }

        MafiaNet::VariableDeltaSerializer::DeserializationContext ctx;
        _vds.BeginDeserialize(&ctx, &deserializeParameters->serializationBitstream[0]);
        // Read into a temporary so the server can ignore a client-supplied owner (it is
        // authoritative); clients adopt the owner the server sends.
        uint64_t incomingOwner = ownerGUID;
        _vds.DeserializeVariable(&ctx, incomingOwner);
        if (!IsServerPeer()) {
            ownerGUID = incomingOwner;
        }
        _vds.DeserializeVariable(&ctx, position);
        _vds.DeserializeVariable(&ctx, velocity);
        _vds.DeserializeVariable(&ctx, rotation);
        DeserializeFields(&_vds, &ctx);
        _vds.EndDeserialize(&ctx);
    }

    MafiaNet::RM3ConstructionState NetworkEntity::QueryConstruction(MafiaNet::Connection_RM3 *destinationConnection, MafiaNet::ReplicaManager3 *) {
        // Unused under QUERY_CONNECTION_FOR_REPLICA_LIST; required by the interface.
        return QueryConstruction_ServerConstruction(destinationConnection, IsServerPeer());
    }

    bool NetworkEntity::QueryRemoteConstruction(MafiaNet::Connection_RM3 *sourceConnection) {
        return QueryRemoteConstruction_ServerConstruction(sourceConnection, IsServerPeer());
    }

    MafiaNet::RM3QuerySerializationResult NetworkEntity::QuerySerialization(MafiaNet::Connection_RM3 *destinationConnection) {
        if (IsServerPeer()) {
            // Relay to everyone except the authoritative owner (no echo back to it).
            if (destinationConnection->GetRakNetGUID().g == ownerGUID) {
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
