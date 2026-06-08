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
    class ReplicationManager;
    class EntityRegistry;

    // Field() writes on the sender and reads on the receiver, so a replica's field list stays in sync.
    class FieldSerializer final {
      public:
        FieldSerializer(MafiaNet::VariableDeltaSerializer *vds, MafiaNet::VariableDeltaSerializer::SerializationContext *ctx) : _vds(vds), _serialize(ctx) {}
        FieldSerializer(MafiaNet::VariableDeltaSerializer *vds, MafiaNet::VariableDeltaSerializer::DeserializationContext *ctx) : _vds(vds), _deserialize(ctx) {}

        bool Writing() const {
            return _serialize != nullptr;
        }

        template <typename T>
        void Field(T &value) {
            if (_serialize) {
                _vds->SerializeVariable(_serialize, value);
            }
            else {
                _vds->DeserializeVariable(_deserialize, value);
            }
        }

      private:
        MafiaNet::VariableDeltaSerializer *_vds                                = nullptr;
        MafiaNet::VariableDeltaSerializer::SerializationContext *_serialize    = nullptr;
        MafiaNet::VariableDeltaSerializer::DeserializationContext *_deserialize = nullptr;
    };

    // A replicated game object. Game entities derive from this and override SerializeFields (per-tick
    // delta state) and/or OnSerializeConstruction (one-shot spawn state).
    //
    // Authority is keyed on ownerGUID: the server serializes to everyone except the owner, the owning
    // client serializes upstream, and Deserialize accepts state only from the current owner.
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

        // --- Server-only streaming metadata (never replicated; unused on the client) ---
        // Grouped under `streaming` so the server-only nature is explicit and these don't read as
        // per-entity wire state. Dimension lives in the VirtualWorldReplica3 base (Get/SetVirtualWorld).
        struct Streaming {
            bool alwaysVisible = false;  // bypass interest culling; replicated to everyone
            bool visible       = true;   // master visibility switch
            bool isViewer      = false;  // drives a connection's interest set (the player's avatar)
            float range        = 100.0f; // interest radius (world units) when acting as a viewer
        };
        Streaming streaming;

        // --- Game extension points ---
        virtual void OnSerializeConstruction(MafiaNet::BitStream *bs, bool write) {
            (void)bs;
            (void)write;
        }
        virtual void SerializeFields(FieldSerializer &fields) {
            (void)fields;
        }
        virtual void OnConstructed() {}

        // Server -> owner override of an owned entity (the owner is otherwise authoritative). Default
        // carries the transform; override to add state, e.g. a vehicle's engine/config.
        virtual void SerializeForcedState(MafiaNet::BitStream *bs, bool write);

        // Called on the owning client after SerializeForcedState has applied the forced fields.
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

        // --- Replica3 implementation (framework-owned; marked final so games extend through the
        // virtual hooks above, never by overriding the wire/authority plumbing) ---
        void WriteAllocationID(MafiaNet::Connection_RM3 *destinationConnection, MafiaNet::BitStream *allocationIdBitstream) const final;
        void SerializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *destinationConnection) final;
        bool DeserializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *sourceConnection) final;
        void SerializeDestruction(MafiaNet::BitStream *destructionBitstream, MafiaNet::Connection_RM3 *destinationConnection) final;
        bool DeserializeDestruction(MafiaNet::BitStream *destructionBitstream, MafiaNet::Connection_RM3 *sourceConnection) final;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) final;

        void OnUserReplicaPreSerializeTick() final;
        MafiaNet::RM3SerializationResult Serialize(MafiaNet::SerializeParameters *serializeParameters) final;
        void Deserialize(MafiaNet::DeserializeParameters *deserializeParameters) final;

        bool QueryRemoteConstruction(MafiaNet::Connection_RM3 *sourceConnection) final;
        MafiaNet::RM3ActionOnPopConnection QueryActionOnPopConnection(MafiaNet::Connection_RM3 *droppedConnection) const final;

      protected:
        // VirtualWorldReplica3 filters by dimension, then delegates the topology decision to these.
        // Protected (matching the base) and final: part of the framework's authority model, not a
        // game extension point.
        MafiaNet::RM3ConstructionState QueryConstructionWithinWorld(MafiaNet::Connection_RM3 *destinationConnection, MafiaNet::ReplicaManager3 *replicaManager3) final;
        MafiaNet::RM3QuerySerializationResult QuerySerializationWithinWorld(MafiaNet::Connection_RM3 *destinationConnection) final;

      private:
        // The owning manager, typed. The base Replica3::replicaManager is a raw ReplicaManager3*;
        // every entity belongs to one of ours, so this downcast is the single sanctioned place for it.
        ReplicationManager *Manager();
        const ReplicationManager *Manager() const;

        // True if we are the server peer (read from the owning ReplicationManager).
        bool IsServerPeer() const;
        uint64_t MyGUID() const;

        // Apply an owner value received over the wire: the server is authoritative and ignores it;
        // clients adopt it. Single source of truth for the rule shared by construction and deltas.
        void AdoptIncomingOwner(uint64_t incomingOwner);

        void SerializeBaseState(MafiaNet::BitStream *bs, bool write);

        // CRC32 of the registered name; stamped by EntityRegistry, not game-settable.
        uint32_t typeId = 0;
        friend class EntityRegistry;

        // Tracks the last value of each serialized variable per connection so updates carry only
        // what changed (the documented ReplicaManager3 delta path).
        MafiaNet::VariableDeltaSerializer _vds;
    };
} // namespace Framework::Networking::Replication
