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

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>

namespace Framework::Networking::Replication {
    // A replicated game object: it owns its state as plain members, and ReplicaManager3 +
    // NetworkIDManager track it while GridSectorizer scopes it. Game-specific entities (player,
    // vehicle, ...) derive from this, add their own fields, and override SerializeFields /
    // DeserializeFields (per-tick state) and/or OnSerializeConstruction / OnDeserializeConstruction
    // (one-shot spawn state).
    //
    // Per-tick updates go through MafiaNet::VariableDeltaSerializer: each variable is compared
    // against the last value sent to a system and transmitted only when it changes. Construction
    // sends a full snapshot. Serialize() returns RM3SR_SERIALIZED_UNIQUELY; ReplicaManager3 calls it
    // per connection while the delta serializer reuses one bitstream within a tick.
    //
    // Authority: QuerySerialization is keyed on ownerGUID — the server serializes to everyone except
    // the owner, the owning client serializes upstream, and Deserialize accepts state only from the
    // current owner so a stale owner cannot write during a handover.
    class NetworkEntity : public MafiaNet::Replica3 {
      public:
        NetworkEntity()           = default;
        ~NetworkEntity() override = default;

        // --- Common replicated state ---
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::quat rotation = glm::identity<glm::quat>();
        glm::vec3 scale    = glm::vec3(1.0f);
        uint64_t modelHash = 0;
        std::string modelName;

        // --- Authority / streaming metadata (server-authoritative) ---
        uint64_t ownerGUID = 0xFFFFFFFFFFFFFFFF; // UNASSIGNED_RAKNET_GUID.g
        int virtualWorld   = 0;
        bool alwaysVisible = false;
        bool isVisible     = true;

        // A viewer is an entity a connection "looks through" (a player's avatar). Its position and
        // streamRange drive that connection's interest set.
        bool isViewer    = false;
        float streamRange = 100.0f;

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

        // Called on the owning client after the server forces a new transform on this entity (its
        // position/rotation have already been applied). Override to apply it to the game world, e.g.
        // teleport the backing object and preload the surrounding world.
        virtual void OnTransformForced() {}

        // Server: push this entity's current position/rotation to its owner. The owner is otherwise
        // authoritative over its own transform, so this is how the server relocates an owned entity
        // (a teleport). No-op for unowned (server-owned) entities, which replicate normally.
        void ForceTransform();

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

        MafiaNet::RM3ConstructionState QueryConstruction(MafiaNet::Connection_RM3 *destinationConnection, MafiaNet::ReplicaManager3 *replicaManager3) override;
        bool QueryRemoteConstruction(MafiaNet::Connection_RM3 *sourceConnection) override;
        MafiaNet::RM3QuerySerializationResult QuerySerialization(MafiaNet::Connection_RM3 *destinationConnection) override;
        MafiaNet::RM3ActionOnPopConnection QueryActionOnPopConnection(MafiaNet::Connection_RM3 *droppedConnection) const override;

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
