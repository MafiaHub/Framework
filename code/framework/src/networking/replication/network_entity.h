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

    // Bidirectional adapter over MafiaNet::VariableDeltaSerializer: the same Field(member) call
    // serializes on the sender and deserializes on the receiver, so a replica's per-tick field list
    // is declared once and the read/write order cannot diverge between peers. NetworkEntity builds
    // one per Serialize()/Deserialize() and passes it to SerializeFields().
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

    // A replicated game object: it owns its state as plain members, and ReplicaManager3 +
    // NetworkIDManager track it while GridSectorizer scopes it. Game-specific entities (player,
    // vehicle, ...) derive from this, add their own fields, and override SerializeFields (per-tick
    // state) and/or OnSerializeConstruction (one-shot spawn state). Every (de)serialization hook is
    // a single bidirectional function taking a `write` flag, so the read and write paths cannot
    // drift out of sync.
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
        // One-shot spawn state, (de)serialized alongside the common construction snapshot. Append
        // your fields with bs->Serialize(write, field); one definition serves both directions.
        virtual void OnSerializeConstruction(MafiaNet::BitStream *bs, bool write) {
            (void)bs;
            (void)write;
        }
        // Per-tick delta state. Append your fields with fields.Field(member); the same call writes on
        // the sender and reads on the receiver, so the field order can never diverge between peers.
        virtual void SerializeFields(FieldSerializer &fields) {
            (void)fields;
        }
        // Called once on the client after construction, e.g. to request the backing game object.
        virtual void OnConstructed() {}

        // The server-authoritative state pushed to the owner by ForceState (the owner is otherwise
        // authoritative over its own updates, and the server withholds serialize to it). Default
        // carries the transform; override to send additional state, e.g. a vehicle's engine/config.
        // Single bidirectional definition: append fields with bs->Serialize(write, field).
        virtual void SerializeForcedState(MafiaNet::BitStream *bs, bool write);

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

        // Full one-shot construction snapshot of the common state (transform + owner), bidirectional.
        void SerializeBaseState(MafiaNet::BitStream *bs, bool write);

        // Concrete type id over the wire (CRC32 of the registered name). Stamped by EntityRegistry at
        // construction and written by WriteAllocationID; not game-settable.
        uint32_t typeId = 0;
        friend class EntityRegistry;

        // Tracks the last value of each serialized variable per connection so updates carry only
        // what changed (the documented ReplicaManager3 delta path).
        MafiaNet::VariableDeltaSerializer _vds;
    };
} // namespace Framework::Networking::Replication
