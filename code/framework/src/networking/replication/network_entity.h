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

    class FieldSerializer final {
      public:
        FieldSerializer(MafiaNet::VariableDeltaSerializer *vds, MafiaNet::VariableDeltaSerializer::SerializationContext *ctx) : _vds(vds), _serialize(ctx) {}
        FieldSerializer(MafiaNet::VariableDeltaSerializer *vds, MafiaNet::VariableDeltaSerializer::DeserializationContext *ctx) : _vds(vds), _deserialize(ctx) {}
        FieldSerializer(MafiaNet::BitStream *bs, bool write) : _plain(bs), _writePlain(write) {}

        bool Writing() const {
            return _serialize != nullptr || (_plain != nullptr && _writePlain);
        }

        template <typename T>
        void Field(T &value) {
            if (_plain) {
                _plain->Serialize(_writePlain, value);
            }
            else if (_serialize) {
                _vds->SerializeVariable(_serialize, value);
            }
            else {
                _vds->DeserializeVariable(_deserialize, value);
            }
        }

        // Quantized writers — meaningful only on the plain-bitstream (transform-channel) path; on the
        // VDS delta path they fall back to full precision so a single Serialize body can serve both.
        void Float16(float &value, float min, float max) {
            if (_plain) {
                _plain->SerializeFloat16(_writePlain, value, min, max);
            }
            else {
                Field(value);
            }
        }

        template <typename T>
        void BitsRange(T &value, T min, T max) {
            if (_plain) {
                _plain->SerializeBitsFromIntegerRange(_writePlain, value, min, max);
            }
            else {
                Field(value);
            }
        }

        // Fixed-point float in [min,max] over `bits` bits (plain path only; VDS keeps full precision).
        void FloatBits(float &value, float min, float max, int bits) {
            if (!_plain) {
                Field(value);
                return;
            }
            const uint32_t maxQ = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
            if (_writePlain) {
                float a = (value - min) / (max - min);
                if (a < 0.0f) {
                    a = 0.0f;
                }
                else if (a > 1.0f) {
                    a = 1.0f;
                }
                uint32_t q = static_cast<uint32_t>(a * static_cast<float>(maxQ) + 0.5f);
                _plain->SerializeBitsFromIntegerRange(true, q, 0u, maxQ);
            }
            else {
                uint32_t q = 0;
                _plain->SerializeBitsFromIntegerRange(false, q, 0u, maxQ);
                value = min + (static_cast<float>(q) / static_cast<float>(maxQ)) * (max - min);
            }
        }

      private:
        MafiaNet::VariableDeltaSerializer *_vds                                 = nullptr;
        MafiaNet::VariableDeltaSerializer::SerializationContext *_serialize     = nullptr;
        MafiaNet::VariableDeltaSerializer::DeserializationContext *_deserialize = nullptr;
        MafiaNet::BitStream *_plain                                             = nullptr;
        bool _writePlain                                                        = false;
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
        MafiaNet::PeerGuid ownerGUID = MafiaNet::UNASSIGNED_PEER_GUID;

        // Fences server overrides against in-flight owner updates: bumped by the server on
        // ForceState, adopted by the owner from the ForceState/SetOwner RPCs and echoed back in its
        // updates; the server drops owner state carrying a stale value (sent before the owner saw the
        // override) instead of letting it revert the forced state. Equality-checked, so uint8 wrap is
        // harmless. Managed by ReplicationManager — game code never touches it.
        uint8_t stateEpoch = 0;

        // Local-clock send time of the last applied update (MafiaNet shifts it on receipt). Not replicated.
        MafiaNet::Time lastUpdateTime = 0;

        // --- Server-only streaming metadata (never replicated; unused on the client) ---
        // Grouped under `streaming` so the server-only nature is explicit and these don't read as
        // per-entity wire state. Dimension lives in the VirtualWorldReplica3 base (Get/SetVirtualWorld).
        struct Streaming {
            bool alwaysVisible = false;  // bypass range culling; dimension still applies
            bool visible       = true;   // master visibility switch
            bool isViewer      = false;  // drives a connection's interest set (the player's avatar)
            float range        = 100.0f; // interest radius (world units) when acting as a viewer
        };
        Streaming streaming;

        // Fate of a delegatable entity when no client can take it.
        enum class OrphanMode : uint8_t {
            Freeze,  // return to the server (UNASSIGNED), keep replicating its last pose
            Destroy, // remove it
        };

        // Server-only, never replicated. Opt-in via delegatable; see ReplicationManager::RunDelegation.
        struct Delegation {
            bool delegatable      = false;
            OrphanMode orphanMode = OrphanMode::Freeze;
            // Pin the syncer to a client, bypassing election. UNASSIGNED elects normally.
            MafiaNet::PeerGuid pinnedOwner = MafiaNet::UNASSIGNED_PEER_GUID;
        };
        Delegation delegation;

        // --- Game extension points ---
        virtual void OnSerializeConstruction(FieldSerializer &fields) {
            (void)fields;
        }
        virtual void SerializeFields(FieldSerializer &fields) {
            (void)fields;
        }
        virtual void OnConstructed() {}

        // Transform tier: the high-frequency pose, written raw to the unreliable serialize channel
        // every tick (ReplicaManager3 memcmp-dedupes it, so a motionless entity stops sending).
        // Override to quantize or extend; the default carries position/velocity/rotation.
        virtual void SerializeTransform(FieldSerializer &fields);

        // Called at the end of every per-tick Deserialize. transformUpdated is true when the update
        // carried the transform channel — the seam an interpolating receiver uses to push a snapshot.
        virtual void OnDeserialized(bool transformUpdated) {
            (void)transformUpdated;
        }

        // Server -> owner override of an owned entity (the owner is otherwise authoritative). Default
        // carries the transform; override to add state, e.g. a vehicle's engine/config.
        virtual void SerializeForcedState(FieldSerializer &fields);

        // Called on the owning client after SerializeForcedState has applied the forced fields.
        virtual void OnStateForced() {}

        // Client-only: authority over this entity crossed our peer. On gain, the replicated pose is
        // already current, so seed the local sim from it; on loss, tear it down.
        virtual void OnOwnershipChanged(bool nowOwner) {
            (void)nowOwner;
        }

        // Server: veto electing `candidate` as syncer (proximity/dimension already applied).
        virtual bool CanDelegateTo(MafiaNet::PeerGuid candidate) const {
            (void)candidate;
            return true;
        }

        // Server: push this entity's forced state to its owner. No-op for unowned (server-owned)
        // entities, which replicate to everyone normally.
        void ForceState();

        // Server: change this entity's owner. The new owner is told directly (the server withholds
        // serialize to an owner, so it would otherwise never learn it gained authority); other peers
        // and a revoked previous owner pick up the change through normal serialization. Pass
        // MafiaNet::UNASSIGNED_PEER_GUID to return ownership to the server.
        void SetOwner(MafiaNet::PeerGuid guid);

        // Client-side: adopt an owner from the server, firing OnOwnershipChanged on a flip. No-op on
        // the server. The single sink for client owner changes so the callback can't be bypassed.
        void SetOwnerFromServer(MafiaNet::PeerGuid newOwner);

        // True on the peer with authority over this entity: the owning client, or the server for
        // server-owned entities. The game decides what owning means (bind the local avatar, drive
        // updates upstream, ...); this just answers who holds authority.
        bool IsOwner() const;

        MafiaNet::Time GetUpdateAge() const;
        glm::vec3 GetExtrapolatedPosition() const;

        void WriteAllocationID(MafiaNet::Connection_RM3 *destinationConnection, MafiaNet::BitStream *allocationIdBitstream) const final;
        void SerializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *destinationConnection) final;
        bool DeserializeConstruction(MafiaNet::BitStream *constructionBitstream, MafiaNet::Connection_RM3 *sourceConnection) final;
        void SerializeDestruction(MafiaNet::BitStream *destructionBitstream, MafiaNet::Connection_RM3 *destinationConnection) final;
        bool DeserializeDestruction(MafiaNet::BitStream *destructionBitstream, MafiaNet::Connection_RM3 *sourceConnection) final;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        void OnUserReplicaPreSerializeTick() final;
        MafiaNet::RM3SerializationResult Serialize(MafiaNet::SerializeParameters *serializeParameters) final;
        void Deserialize(MafiaNet::DeserializeParameters *deserializeParameters) final;

        bool QueryRemoteConstruction(MafiaNet::Connection_RM3 *sourceConnection) final;
        MafiaNet::RM3ActionOnPopConnection QueryActionOnPopConnection(MafiaNet::Connection_RM3 *droppedConnection) const final;

      protected:
        // VirtualWorldReplica3 filters by dimension, then delegates the topology decision to these.
        MafiaNet::RM3ConstructionState QueryConstructionWithinWorld(MafiaNet::Connection_RM3 *destinationConnection, MafiaNet::ReplicaManager3 *replicaManager3) final;
        MafiaNet::RM3QuerySerializationResult QuerySerializationWithinWorld(MafiaNet::Connection_RM3 *destinationConnection) final;

      private:
        // The owning manager, typed. The base Replica3::replicaManager is a raw ReplicaManager3*;
        // every entity belongs to one of ours, so this downcast is the single sanctioned place for it.
        ReplicationManager *Manager();
        const ReplicationManager *Manager() const;

        // True if we are the server peer (read from the owning ReplicationManager).
        bool IsServerPeer() const;
        MafiaNet::PeerGuid MyGUID() const;

        // Apply an owner value received over the wire: the server is authoritative and ignores it;
        // clients adopt it. Single source of truth for the rule shared by construction and deltas.
        void AdoptIncomingOwner(MafiaNet::PeerGuid incomingOwner);

        // The base reliable field set (owner authority) in its single wire order, shared by
        // construction, Serialize, and Deserialize so the three paths cannot drift apart. The
        // transform travels on its own unreliable channel (SerializeTransform); the state epoch
        // travels as a raw prefix on each channel — see the comments in Serialize().
        void SerializeBaseFields(FieldSerializer &fields);

        // Apply an epoch received over the wire: clients adopt it; the server compares it and
        // returns false when the update is stale (sent before the last ForceState) and must not be
        // applied.
        bool ApplyIncomingEpoch(uint8_t incomingEpoch);

        // Set during DeserializeConstruction: defers OnOwnershipChanged to the end, once the pose is read.
        bool _constructing = false;

        // CRC32 of the registered name; stamped by EntityRegistry, not game-settable.
        uint32_t typeId = 0;
        friend class EntityRegistry;

        // Tracks the last value of each serialized variable per connection so updates carry only
        // what changed (the documented ReplicaManager3 delta path).
        MafiaNet::VariableDeltaSerializer _vds;
    };
} // namespace Framework::Networking::Replication
