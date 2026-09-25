/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "state_bag.h"

#include <mafianet/ReplicaManager3.h>
#include <mafianet/VariableDeltaSerializer.h>
#include <mafianet/VirtualWorldReplica3.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace Framework::Networking::Replication {
    class ReplicationManager;
    class EntityRegistry;
    struct NametagState;
    struct DelegationPolicy;

    // `serverAuthored` says whose bytes these are: true when the server writes the stream (the per-tick
    // channels it relays, construction, forced state), false when an owning client writes it upstream.
    // Both ends of one stream agree on it, which is what lets ServerField leave a field out of the
    // upstream wire entirely.
    class FieldSerializer final {
      public:
        FieldSerializer(MafiaNet::VariableDeltaSerializer *vds, MafiaNet::VariableDeltaSerializer::SerializationContext *ctx, bool serverAuthored) : _vds(vds), _serialize(ctx), _serverAuthored(serverAuthored) {}
        FieldSerializer(MafiaNet::VariableDeltaSerializer *vds, MafiaNet::VariableDeltaSerializer::DeserializationContext *ctx, bool serverAuthored) : _vds(vds), _deserialize(ctx), _serverAuthored(serverAuthored) {}
        FieldSerializer(MafiaNet::BitStream *bs, bool write, bool serverAuthored = true) : _plain(bs), _writePlain(write), _serverAuthored(serverAuthored) {}

        bool Writing() const {
            return _serialize != nullptr || (_plain != nullptr && _writePlain);
        }

        // Whether the server wrote (or is writing) this stream; see the class comment.
        bool ServerAuthored() const {
            return _serverAuthored;
        }

        bool Good() const {
            return _good;
        }

        template <typename T>
        void Field(T &value) {
            if (!_good) {
                return;
            }
            if (_plain) {
                _good = _plain->Serialize(_writePlain, value);
            }
            else if (_serialize) {
                _vds->SerializeVariable(_serialize, value);
            }
            else {
                // DeserializeVariable returns "changed", not read success.
                bool changed = false;
                _good = _deserialize->bitStream->Read(changed);
                if (_good && changed) {
                    _good = _deserialize->bitStream->Read(value);
                }
            }
        }

        // A field only the server decides -- a verdict, a seat, a name -- on an entity a client owns.
        // Carried wherever the server writes, and absent from the owner's upstream stream on both
        // ends: the owner cannot echo back a value the server has since changed, and a forced state
        // pushed for something else cannot be undone by the owner's in-flight copy of it. The owner
        // learns it from SerializeForcedState, which the server writes.
        template <typename T>
        void ServerField(T &value) {
            if (_serverAuthored) {
                Field(value);
            }
        }

        // Quantized writers — meaningful only on the plain-bitstream (transform-channel) path; on the
        // VDS delta path they fall back to full precision so a single Serialize body can serve both.
        void Float16(float &value, float min, float max) {
            if (!_good) {
                return;
            }
            if (_plain) {
                _good = _plain->SerializeFloat16(_writePlain, value, min, max);
            }
            else {
                Field(value);
            }
        }

        template <typename T>
        void BitsRange(T &value, T min, T max) {
            if (!_good) {
                return;
            }
            if (_plain) {
                _good = _plain->SerializeBitsFromIntegerRange(_writePlain, value, min, max);
            }
            else {
                Field(value);
            }
        }

        // Fixed-point float in [min,max] over `bits` bits (plain path only; VDS keeps full precision).
        void FloatBits(float &value, float min, float max, int bits) {
            if (!_good) {
                return;
            }
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
                _good = _plain->SerializeBitsFromIntegerRange(true, q, 0u, maxQ);
            }
            else {
                uint32_t q = 0;
                _good = _plain->SerializeBitsFromIntegerRange(false, q, 0u, maxQ);
                if (_good) {
                    value = min + (static_cast<float>(q) / static_cast<float>(maxQ)) * (max - min);
                }
            }
        }

      private:
        MafiaNet::VariableDeltaSerializer *_vds                                 = nullptr;
        MafiaNet::VariableDeltaSerializer::SerializationContext *_serialize     = nullptr;
        MafiaNet::VariableDeltaSerializer::DeserializationContext *_deserialize = nullptr;
        MafiaNet::BitStream *_plain                                             = nullptr;
        bool _writePlain                                                        = false;
        bool _serverAuthored                                                    = true;
        bool _good                                                              = true;
    };

    // A replicated game object. Game entities derive from this and override SerializeFields (per-tick
    // delta state) and/or OnSerializeConstruction (one-shot spawn state).
    //
    // Authority is keyed on ownerGUID: the server serializes to everyone except the owner, the owning
    // client serializes upstream, and Deserialize accepts state only from the current owner.
    class NetworkEntity : public MafiaNet::VirtualWorldReplica3 {
      public:
        NetworkEntity() {
            state.Bind(this);
        }
        ~NetworkEntity() override = default;

        // The bag points back at its entity, so a copy would raise the original's changes. Nothing
        // legitimately copies an entity; this makes the mistake a compile error.
        NetworkEntity(const NetworkEntity &)            = delete;
        NetworkEntity &operator=(const NetworkEntity &) = delete;

        // --- Common replicated state ---
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::quat rotation = glm::identity<glm::quat>();

        // --- Authority (replicated) ---
        MafiaNet::PeerGuid ownerGUID = MafiaNet::UNASSIGNED_PEER_GUID;

        // Arbitrary key/value state a game or a script hangs off this entity, replicated on its own
        // RPC (state_bag.h explains why not through SerializeFields). A bag nobody writes costs a
        // zero count per construction and nothing else.
        StateBag state;

        // Idle refresh of an unchanged pose, so a lost final packet is repaired: every kTransformRefreshMs
        // for kTransformRefreshBurstMs after the last change, then every kTransformHeartbeatMs. Entities
        // that never moved since construction are not refreshed.
        static constexpr MafiaNet::Time kTransformRefreshMs      = 500;
        static constexpr MafiaNet::Time kTransformRefreshBurstMs = 2000;
        static constexpr MafiaNet::Time kTransformHeartbeatMs    = 5000;

        // Fences server overrides against in-flight owner updates: bumped by the server on
        // ForceState, adopted by the owner from the ForceState/SetOwner RPCs and echoed back in its
        // updates; the server drops owner state carrying a stale value (sent before the owner saw the
        // override) instead of letting it revert the forced state. Equality-checked, so uint8 wrap is
        // harmless. Managed by ReplicationManager — game code never touches it.
        uint8_t stateEpoch = 0;

        // Local-clock send time of the last applied update (MafiaNet shifts it on receipt). Not replicated.
        MafiaNet::Time lastUpdateTime = 0;

        // --- Server-side streaming metadata (not replicated, isViewer aside; unused on the client) ---
        // Grouped under `streaming` so the server-only nature is explicit and these don't read as
        // per-entity wire state. Dimension lives in the VirtualWorldReplica3 base (Get/SetVirtualWorld).
        // isViewer is the exception: ReplicationManager::SetViewer sets it and the base fields carry
        // it, because a client has no other way to tell a player's avatar from an entity the player
        // merely owns (see ReplicationManager::ForEachAvatar).
        struct Streaming {
            bool alwaysVisible = false;  // bypass range culling; dimension still applies
            bool visible       = true;   // master visibility switch
            bool isViewer      = false;  // drives a connection's interest set (the player's avatar); replicated
            float range        = 100.0f; // interest radius (world units) when acting as a viewer
            MafiaNet::PeerGuid targetGUID = MafiaNet::UNASSIGNED_PEER_GUID; // if set, streams only to this connection
        };
        Streaming streaming;

        // Type id (CRC32 of the registered name), stamped by EntityRegistry.
        uint32_t GetTypeId() const {
            return _typeId;
        }

        // --- Game extension points ---
        // Server interest: an entity this one makes no sense without (a seated ped needs its car).
        // Survivors of a per-type budget pull it in with them, uncounted. Followed one level.
        virtual NetworkEntity *GetInterestDependency() {
            return nullptr;
        }

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

        // Server: whether the pose this entity holds was put there by the server -- a script or game
        // code moved it -- rather than being the pose its owner last reported. True for an entity
        // no owner has reported a pose for yet, whose only pose is the server's. ForceState carries
        // the answer, which the owner reads as WasPoseForced().
        bool IsPoseServerAuthored() const;

        // Owning client, from OnStateForced on: whether the last forced state's pose is one the
        // server authored (a teleport the owner has to take) rather than its own report echoed back.
        // A push made for some other field still carries the pose, a round trip old, and an owner
        // that warps onto every one of those jerks backwards each time.
        bool WasPoseForced() const {
            return _poseForced;
        }

        // Raised for every key a construction seed delivered, after OnConstructed, so a listener
        // finds the entity fully built.
        void NotifySeededState(const std::vector<std::string> &keys);

        // This entity's nametag state, for entities that carry one (games embed and serialize it).
        virtual NametagState *GetNametag() {
            return nullptr;
        }

        // --- Delegated simulation (server-side; see delegation.h) ---
        // An entity the server owns but cannot simulate returns a policy here, and DelegationManager
        // elects a client to run it: that client becomes the owner, so the whole authority model
        // above applies to it unchanged. Null (the default) means the entity is never delegated.
        //
        // The pointer must outlive the entity and is read on every election pass, so the usual
        // implementation returns the address of a static or member policy.
        virtual const DelegationPolicy *GetDelegationPolicy() const {
            return nullptr;
        }

        // Asked on the server when an election wants to move this entity, and only then. False
        // defers the handover -- a body mid-attack or mid-climb has state on its simulator that no
        // other peer can continue -- until the entity agrees or the policy's grace runs out.
        virtual bool CanReleaseSimulation() const {
            return true;
        }

        // Raised on the server after a delegated entity's simulator changed and its forced state has
        // been pushed. UNASSIGNED on either side means "the server", i.e. nobody was or is
        // simulating it. The game's own bookkeeping (revisions, script events) hangs off this.
        virtual void OnSimulatorChanged(MafiaNet::PeerGuid previous, MafiaNet::PeerGuid current) {
            (void)previous;
            (void)current;
        }

        // True on the client that has been elected to simulate this entity. Identical to IsOwner()
        // for a delegated entity; it exists so game code reads as what it means at the call site.
        bool IsSimulating() const {
            return GetDelegationPolicy() != nullptr && IsOwner();
        }

        // Server: push this entity's forced state to its owner. No-op for unowned (server-owned)
        // entities, which replicate to everyone normally.
        void ForceState();

        // Server: change this entity's owner. The new owner is told directly (the server withholds
        // serialize to an owner, so it would otherwise never learn it gained authority); other peers
        // and a revoked previous owner pick up the change through normal serialization. Pass
        // MafiaNet::UNASSIGNED_PEER_GUID to return ownership to the server.
        void SetOwner(MafiaNet::PeerGuid guid);

        // True on the peer with authority over this entity: the owning client, or the server for
        // server-owned entities. The game decides what owning means (bind the local avatar, drive
        // updates upstream, ...); this just answers who holds authority.
        bool IsOwner() const;

        MafiaNet::Time GetUpdateAge() const;
        glm::vec3 GetExtrapolatedPosition() const;

        // Accept the next pose whatever its timestamp; called on an ownership change.
        void ResetTransformOrdering() {
            _lastTransformTime = 0;
        }

        // Resolve another entity in the same replicated world by NetworkID; the owning manager is
        // otherwise private. The seam for overrides that need a sibling, chiefly
        // GetInterestDependency.
        NetworkEntity *ResolveSibling(MafiaNet::NetworkID networkId) const;

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

        // The base reliable field set (owner authority, avatar flag) in its single wire order, shared by
        // construction, Serialize, and Deserialize so the three paths cannot drift apart. The
        // transform travels on its own unreliable channel (SerializeTransform); the state epoch
        // travels as a raw prefix on each channel — see the comments in Serialize().
        void SerializeBaseFields(FieldSerializer &fields);

        // Apply an epoch received over the wire: clients adopt it; the server compares it and
        // returns false when the update is stale (sent before the last ForceState) and must not be
        // applied.
        bool ApplyIncomingEpoch(uint8_t incomingEpoch);

        // --- State-bag plumbing, reached only by the bag itself ---
        // Registers with the manager's dirty list, so a flush walks what changed rather than
        // sweeping the world.
        void MarkStateDirty();
        // Raised on both peers: the server on write, a client on apply.
        void NotifyStateChanged(const StateChange &change);
        friend class StateBag;

        // CRC32 of the registered name; stamped by EntityRegistry, not game-settable.
        uint32_t _typeId = 0;
        friend class EntityRegistry;

        // Tracks the last value of each serialized variable per connection so updates carry only
        // what changed (the documented ReplicaManager3 delta path).
        MafiaNet::VariableDeltaSerializer _vds;

        // Sender: refresh schedule state, in serialize-tick time.
        bool _transformMoved                = false;
        MafiaNet::Time _lastTransformChange = 0;
        MafiaNet::Time _lastTransformSend   = 0;

        // Receiver: send time of the newest applied pose. The transform channel is plain Unreliable
        // (a sequenced stream is per channel, not per entity), so this is the per-entity ordering.
        MafiaNet::Time _lastTransformTime = 0;

        // Server: the pose the owner last reported, so a forced state can tell a move the server
        // made from the owner's own pose echoed back. Compared exactly: both sides hold the value the
        // transform channel decoded, and anything that writes a different one is a move.
        glm::vec3 _ownerPosition = glm::vec3(0.0f);
        glm::quat _ownerRotation = glm::identity<glm::quat>();
        bool _ownerPoseKnown     = false;

        // Owning client: the flag the last forced-state RPC carried. Written by the manager.
        bool _poseForced = false;
        friend class ReplicationManager;
    };
} // namespace Framework::Networking::Replication
