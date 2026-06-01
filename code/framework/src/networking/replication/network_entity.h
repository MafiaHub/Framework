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
    // A NetworkEntity IS the replicated object — it owns its state directly as plain members. There
    // is no ECS / flecs behind it; ReplicaManager3 + NetworkIDManager track it and GridSectorizer
    // scopes it. Game-specific entities (player, vehicle, ...) derive from this, add their own
    // fields, and override SerializeFields/DeserializeFields (per-tick delta state) and/or
    // OnSerializeConstruction/OnDeserializeConstruction (one-shot spawn state).
    //
    // Per-tick updates use MafiaNet::VariableDeltaSerializer (the documented ReplicaManager3 delta
    // path): each variable is compared against the last value sent to a system and only transmitted
    // when it changes. Construction sends a full snapshot once. Serialize() therefore returns
    // RM3SR_SERIALIZED_UNIQUELY — ReplicaManager3 calls it per connection, while the delta serializer
    // internally reuses the identical-broadcast bitstream within a tick for efficiency.
    //
    // Authority (validated design point C1): we override QuerySerialization ourselves keyed on
    // ownerGUID, not the stock creatingSystemGUID helpers. The server serializes to everyone except
    // the owner; the owning client serializes upstream. Deserialize is gated on the current owner so
    // a stale owner cannot write during a handover.
    class NetworkEntity : public MafiaNet::Replica3 {
      public:
        NetworkEntity()           = default;
        ~NetworkEntity() override = default;

        // --- Common replicated state (owned, no ECS) ---
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
