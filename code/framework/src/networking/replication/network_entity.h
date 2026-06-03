/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/ReplicaManager3.h>
#include <mafianet/VariableDeltaSerializer.h>
#include <mafianet/VirtualWorldReplica3.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace Framework::Networking::Replication {
    // A replicated game object: it owns its state as plain members, and ReplicaManager3 +
    // NetworkIDManager track it while GridSectorizer scopes it. Game-specific entities (player,
    // vehicle, ...) derive from this, add their own fields, and override SerializeFields /
    // DeserializeFields (per-tick state) and/or OnSerializeConstruction / OnDeserializeConstruction
    // (one-shot spawn state).
    //
    // Per-tick updates go through MafiaNet::VariableDeltaSerializer: each variable is compared
    // against the last value sent to a system and transmitted only when it changes. Construction
    // sends a full snapshot. Serialize() returns RM3SR_BROADCAST_IDENTICALLY: the delta serializer
    // builds one bitstream once per tick and ReplicaManager3 reuses those bytes for every
    // connection (per-connection owner filtering still happens in QuerySerializationWithinWorld).
    //
    // Authority: QuerySerialization is keyed on ownerGUID — the server serializes to everyone except
    // the owner, the owning client serializes upstream, and Deserialize accepts state only from the
    // current owner so a stale owner cannot write during a handover.
    class NetworkEntity : public MafiaNet::VirtualWorldReplica3 {
      public:
        NetworkEntity()           = default;
        ~NetworkEntity() override = default;

        // --- Common replicated state ---
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::quat rotation = glm::identity<glm::quat>();

        // --- Authority (replicated) ---
        uint64_t ownerGUID = MafiaNet::UNASSIGNED_RAKNET_GUID.g;

        // Local-clock send time of the last applied update (MafiaNet shifts it on receipt). Not replicated.
        MafiaNet::Time lastUpdateTime = 0;

        // --- Server-only streaming metadata (not replicated) ---
        // Dimension lives in the VirtualWorldReplica3 base (Get/SetVirtualWorld).
        bool alwaysVisible = false;
        bool isVisible     = true;
        bool isViewer      = false;
        float streamRange  = 100.0f;

        // Set by the EntityFactory on construction; identifies the concrete type over the wire.
        uint32_t typeId = 0;

        // --- Game extension points ---
        // One-shot spawn state, written/read alongside the common construction snapshot.
        virtual void OnSerializeConstruction(MafiaNet::BitStream *bs) {
            (void)bs;
        }
        virtual void OnDeserializeConstruction(MafiaNet::BitStream *bs) {
            (void)bs;
        }
        // Per-tick delta state. Append your fields with vds->SerializeVariable(ctx, field) /
        // vds->DeserializeVariable(ctx, field), in the same order on both sides.
        virtual void SerializeFields(MafiaNet::VariableDeltaSerializer *vds, MafiaNet::VariableDeltaSerializer::SerializationContext *ctx) {
            (void)vds;
            (void)ctx;
        }
        virtual void DeserializeFields(MafiaNet::VariableDeltaSerializer *vds, MafiaNet::VariableDeltaSerializer::DeserializationContext *ctx) {
            (void)vds;
            (void)ctx;
        }
        // Called once on the client after construction, e.g. to request the backing game object.
        virtual void OnConstructed() {}

        // The server-authoritative state pushed to the owner by ForceState (the owner is otherwise
        // authoritative over its own updates, and the server withholds serialize to it). Default
        // carries the transform; override to send additional state, e.g. a vehicle's engine/config.
        // Must read/write symmetrically.
        virtual void WriteForcedState(MafiaNet::BitStream *bs) const;
        virtual void ReadForcedState(MafiaNet::BitStream *bs);

        // Called on the owning client after ReadForcedState has applied the forced fields. Override
        // to push them into the game (teleport and preload the world, set the engine, ...).
        virtual void OnStateForced() {}

        // Server: push this entity's forced state to its owner. No-op for unowned (server-owned)
        // entities, which replicate to everyone normally.
        void ForceState();

        // Server: change this entity's owner. The new owner is told directly (the server withholds
        // serialize to an owner, so it would otherwise never learn it gained authority); other peers
        // and a revoked previous owner pick up the change through normal serialization. Pass
        // UNASSIGNED_RAKNET_GUID.g to return ownership to the server.
        void SetOwner(uint64_t guid);

        // True on the peer with authority over this entity: the owning client, or the server for
        // server-owned entities. The game decides what owning means (bind the local avatar, drive
        // updates upstream, ...); this just answers who holds authority.
        bool IsOwner() const;

        MafiaNet::Time GetUpdateAge() const;
        glm::vec3 GetExtrapolatedPosition() const;

        // --- Replica3 implementation ---
        void WriteAllocationID(MafiaNet::Connection_RM3 *destinationConnection, MafiaNet::BitStream *allocationIdBitstream) const override;
        void SerializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *destinationConnection) override;
        bool DeserializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *sourceConnection) override;
        void SerializeDestruction(MafiaNet::BitStream *destructionBitstream, MafiaNet::Connection_RM3 *destinationConnection) override;
        bool DeserializeDestruction(MafiaNet::BitStream *destructionBitstream, MafiaNet::Connection_RM3 *sourceConnection) override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        void OnUserReplicaPreSerializeTick() override;
        MafiaNet::RM3SerializationResult Serialize(MafiaNet::SerializeParameters *serializeParameters) override;
        void Deserialize(MafiaNet::DeserializeParameters *deserializeParameters) override;

        bool QueryRemoteConstruction(MafiaNet::Connection_RM3 *sourceConnection) override;
        MafiaNet::RM3ActionOnPopConnection QueryActionOnPopConnection(MafiaNet::Connection_RM3 *droppedConnection) const override;

        // VirtualWorldReplica3 filters by dimension, then delegates the topology decision to these.
        MafiaNet::RM3ConstructionState QueryConstructionWithinWorld(MafiaNet::Connection_RM3 *destinationConnection, MafiaNet::ReplicaManager3 *replicaManager3) override;
        MafiaNet::RM3QuerySerializationResult QuerySerializationWithinWorld(MafiaNet::Connection_RM3 *destinationConnection) override;

      private:
        // True if we are the server peer (read from the owning ReplicationManager).
        bool IsServerPeer() const;
        uint64_t MyGUID() const;
        // Full one-shot construction snapshot of the common state.
        void WriteConstruction(MafiaNet::BitStream *bs) const;
        void ReadConstruction(MafiaNet::BitStream *bs);

        // Tracks the last value of each serialized variable per connection so updates carry only
        // what changed (the documented ReplicaManager3 delta path).
        MafiaNet::VariableDeltaSerializer _vds;
    };
} // namespace Framework::Networking::Replication
