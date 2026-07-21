/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "replication_manager.h"

#include "../network_peer.h"
#include "entity_registry.h"
#include "replication_connection.h"

#include <chrono>

namespace Framework::Networking::Replication {
    namespace {
        // Raw RPC: the tail is the entity's polymorphic SerializeForcedState payload.
        constexpr const char *kForceStateId = "Framework::ForceState";

        struct SetOwnerRPC {
            static constexpr const char *kIdentifier = "Framework::SetOwner";

            MafiaNet::NetworkID networkId;
            MafiaNet::PeerGuid ownerGUID {};
            // The new owner adopts the server's current epoch, or every update it sends would be
            // rejected as stale (see NetworkEntity::stateEpoch).
            uint8_t stateEpoch = 0;

            void Serialize(MafiaNet::BitStream *bs, bool write) {
                bs->SerializeCompressed(write, networkId);
                bs->Serialize(write, ownerGUID);
                bs->Serialize(write, stateEpoch);
            }
        };
    } // namespace

    ReplicationManager::ReplicationManager()  = default;
    ReplicationManager::~ReplicationManager() = default;

    void ReplicationManager::ConfigureGrid(float cellSize, float worldMin, float worldMax) {
        _interest.Configure(cellSize, worldMin, worldMax);
    }

    void ReplicationManager::Init(NetworkPeer *owner, bool isServer) {
        _owner    = owner;
        _isServer = isServer;
        _myGUID   = MafiaNet::ToPeerGuid(owner->GetPeer()->GetMyGUID());
        SetNetworkIDManager(owner->GetNetworkIDManager());
        owner->GetPeer()->AttachPlugin(this);

        // Client-only: these are server->owner pushes, so the server must never accept them inbound.
        if (!_isServer && !_clientRPCsRegistered) {
            owner->RegisterRawRPC(kForceStateId, [this](MafiaNet::BitStream *bs, MafiaNet::Packet *) {
                MafiaNet::NetworkID networkId;
                bs->ReadCompressed(networkId);
                uint8_t epoch = 0;
                bs->Read(epoch);
                if (auto *entity = GetEntityByNetworkID(networkId)) {
                    // Adopting the epoch is what acknowledges the override: updates we send from here
                    // on carry it, so the server stops dropping them.
                    entity->stateEpoch = epoch;
                    FieldSerializer fields(bs, false);
                    entity->SerializeForcedState(fields);
                    entity->OnStateForced();
                }
            });
            owner->RegisterRPC<SetOwnerRPC>([this](const SetOwnerRPC &payload, MafiaNet::Packet *) {
                if (auto *entity = GetEntityByNetworkID(payload.networkId)) {
                    entity->ownerGUID  = payload.ownerGUID;
                    entity->stateEpoch = payload.stateEpoch;
                }
            });
            _clientRPCsRegistered = true;
        }

        InitMetrics();
    }

    void ReplicationManager::InitMetrics() {
        auto &reg           = Metrics::Registry::Get();
        _replCreated        = reg.RegisterCounter("fw_repl_entities_created_total", "Replicated entities created");
        _replDestroyed      = reg.RegisterCounter("fw_repl_entities_destroyed_total", "Replicated entities destroyed");
        _interestCandidates = reg.RegisterCounter("fw_repl_interest_candidates_total", "Entities evaluated for interest per query (pre-filter)");
        _interestSelected   = reg.RegisterCounter("fw_repl_interest_selected_total", "Entities added to an interest set per query (post-filter)");
        _interestRebuildDur        = reg.RegisterHistogram("fw_repl_interest_rebuild_duration_seconds", "Interest index rebuild duration", Metrics::Buckets::Exponential(0.00005, 2.0, 8));
        _forcedStates              = reg.RegisterCounter("fw_repl_forced_states_total", "Server->owner forced-state overrides sent");
        _ownerChanges              = reg.RegisterCounter("fw_repl_owner_changes_total", "Entity ownership changes");
        _staleEpochTransform       = reg.RegisterCounter("fw_repl_stale_epoch_drops_total", "Owner updates dropped for stale state epoch", {{"channel", "transform"}});
        _staleEpochState           = reg.RegisterCounter("fw_repl_stale_epoch_drops_total", "Owner updates dropped for stale state epoch", {{"channel", "state"}});
        _rejectedNonOwner          = reg.RegisterCounter("fw_repl_rejected_nonowner_updates_total", "Updates rejected by the server authority gate (non-owner sender)");
        _serializeDuration         = reg.RegisterHistogram("fw_repl_serialize_duration_seconds", "Per-entity replication serializer wall time", Metrics::Buckets::Exponential(0.00001, 4.0, 9));
        _serializedEntities        = reg.RegisterCounter("fw_repl_serialized_entities_total", "Entity serializer invocations");
        _serializedBytes           = reg.RegisterCounter("fw_repl_serialized_bytes_total", "Replication payload bytes produced before transport processing");
        _serializedEntitiesPerTick = reg.RegisterHistogram("fw_repl_serialized_entities_per_tick", "Entity serializer invocations between network ticks", Metrics::Buckets::Explicit({1, 4, 16, 64, 256, 1024, 4096, 16384}));
        _serializedBytesPerTick    = reg.RegisterHistogram("fw_repl_serialized_bytes_per_tick", "Replication payload bytes produced between network ticks", Metrics::Buckets::Explicit({64, 256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304}));
        _replEntities = reg.RegisterGauge("fw_repl_entities", "Live replicated entities");
        _replEntities->Set(0.0);
    }

    void ReplicationManager::RecordStaleEpochDrop(int channel) {
        if (channel == 0) {
            if (_staleEpochTransform) {
                _staleEpochTransform->Inc();
            }
        }
        else if (_staleEpochState) {
            _staleEpochState->Inc();
        }
    }

    void ReplicationManager::RecordRejectedNonOwnerUpdate() {
        if (_rejectedNonOwner) {
            _rejectedNonOwner->Inc();
        }
    }

    void ReplicationManager::RecordSerialization(double durationSeconds, uint64_t bytes) {
        if (!_isServer) {
            return;
        }
        if (_serializeDuration) {
            _serializeDuration->Observe(durationSeconds);
        }
        if (_serializedEntities) {
            _serializedEntities->Inc();
        }
        if (bytes && _serializedBytes) {
            _serializedBytes->Inc(bytes);
        }
        _serializedEntitiesThisTick.fetch_add(1, std::memory_order_relaxed);
        _serializedBytesThisTick.fetch_add(bytes, std::memory_order_relaxed);
    }

    void ReplicationManager::ForceState(NetworkEntity *entity) {
        // Server-only, like SetOwner: on a client this must be a no-op, not an RPC misaddressed to a
        // peer we aren't connected to (the shared scripting builtins call it on both sides).
        if (!entity || !_owner || !_isServer || entity->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
            return;
        }
        if (_forcedStates) {
            _forcedStates->Inc();
        }
        // The epoch fences the override: the owner echoes the new value back in its updates, and
        // Deserialize drops owner state still carrying the old one — in-flight packets sent before
        // the owner saw this override can no longer revert it.
        ++entity->stateEpoch;
        MafiaNet::BitStream bs;
        MafiaNet::NetworkID networkId = entity->GetNetworkID();
        // NetworkIDs are small and monotonic, so WriteCompressed strips the leading zero bytes.
        bs.WriteCompressed(networkId);
        bs.Write(entity->stateEpoch);
        FieldSerializer fields(&bs, true);
        entity->SerializeForcedState(fields);
        _owner->SendRawRPC(kForceStateId, bs, MafiaNet::ToGuid(entity->ownerGUID));
    }

    void ReplicationManager::SetOwner(NetworkEntity *entity, MafiaNet::PeerGuid guid) {
        if (!entity) {
            return;
        }
        if (_ownerChanges) {
            _ownerChanges->Inc();
        }
        entity->ownerGUID = guid;
        // Serialize to an owner is withheld, so the grant can't ride normal replication: tell the new
        // owner directly. Other peers (and any prior owner) pick it up through serialize.
        if (_owner && _isServer && guid != MafiaNet::UNASSIGNED_PEER_GUID) {
            SetOwnerRPC payload;
            payload.networkId  = entity->GetNetworkID();
            payload.ownerGUID  = guid;
            payload.stateEpoch = entity->stateEpoch;
            _owner->SendRPC(payload, MafiaNet::ToGuid(guid));
        }
    }

    NetworkEntity *ReplicationManager::CreateEntity(uint32_t typeId) {
        NetworkEntity *entity = EntityRegistry::Get().Create(typeId);
        if (!entity) {
            return nullptr;
        }
        // Assign a small, sequential id before Reference() so the NetworkIDManager tracks the entity
        // under it. Ids must stay within JavaScript's 2^53 exact-integer range so scripts can hold
        // them as plain numbers. Clients adopt this id via the construction snapshot.
        entity->SetNetworkID(++_nextNetworkId);
        Reference(entity);
        if (_onEntityCreated) {
            _onEntityCreated(entity->GetNetworkID());
        }
        if (_replCreated) {
            _replCreated->Inc();
        }
        if (_replEntities) {
            _replEntities->Set(static_cast<double>(GetReplicaCount()));
        }
        return entity;
    }

    void ReplicationManager::DestroyEntity(NetworkEntity *entity) {
        if (!entity) {
            return;
        }
        if (_replDestroyed) {
            _replDestroyed->Inc();
        }
        // Erase by value, not by ownerGUID: the owner key may have been reassigned since SetViewer,
        // and on a respawn flow (SetViewer(newAvatar) then DestroyEntity(oldAvatar)) an erase by key
        // would take out the replacement's mapping and silence that connection's streaming.
        for (auto it = _viewers.begin(); it != _viewers.end();) {
            if (it->second == entity) {
                it = _viewers.erase(it);
            }
            else {
                ++it;
            }
        }
        // Scrub the interest indices so this delete can't dangle before the next rebuild.
        _interest.Remove(entity);
        if (_onEntityDestroyed) {
            _onEntityDestroyed(entity->GetNetworkID());
        }
        // BroadcastDestruction must precede deletion; ~Replica3 dereferences automatically.
        entity->BroadcastDestruction();
        delete entity;
        if (_replEntities) {
            _replEntities->Set(static_cast<double>(GetReplicaCount()));
        }
    }

    NetworkEntity *ReplicationManager::GetEntityByNetworkID(MafiaNet::NetworkID networkId) const {
        auto *idm = GetNetworkIDManager();
        if (!idm) {
            return nullptr;
        }
        return idm->GET_OBJECT_FROM_ID<NetworkEntity *>(networkId);
    }

    void ReplicationManager::ForEachEntity(const fu2::function<void(NetworkEntity *) const> &fn) const {
        const unsigned count = GetReplicaCount();
        for (unsigned i = 0; i < count; ++i) {
            auto *entity = static_cast<NetworkEntity *>(GetReplicaAtIndex(i));
            if (entity) {
                fn(entity);
            }
        }
    }

    void ReplicationManager::SetViewer(MafiaNet::PeerGuid guid, NetworkEntity *entity) {
        // A replaced avatar stops being a viewer, or the flag would outlive the mapping and confuse
        // later lifecycle decisions keyed on it.
        if (auto *previous = GetViewer(guid); previous && previous != entity) {
            previous->streaming.isViewer = false;
        }
        if (entity) {
            entity->streaming.isViewer = true;
        }
        _viewers[guid] = entity;
    }

    NetworkEntity *ReplicationManager::GetViewer(MafiaNet::PeerGuid guid) const {
        const auto it = _viewers.find(guid);
        return it != _viewers.end() ? it->second : nullptr;
    }

    void ReplicationManager::ClearViewer(MafiaNet::PeerGuid guid) {
        const auto it = _viewers.find(guid);
        if (it == _viewers.end()) {
            return;
        }
        if (it->second) {
            it->second->streaming.isViewer = false;
        }
        _viewers.erase(it);
    }

    void ReplicationManager::RebuildInterest() {
        if (!_isServer) {
            return;
        }
        const uint64_t entitiesLastTick = _serializedEntitiesThisTick.exchange(0, std::memory_order_relaxed);
        const uint64_t bytesLastTick    = _serializedBytesThisTick.exchange(0, std::memory_order_relaxed);
        if (_hasSerializationWindow) {
            if (_serializedEntitiesPerTick) {
                _serializedEntitiesPerTick->Observe(static_cast<double>(entitiesLastTick));
            }
            if (_serializedBytesPerTick) {
                _serializedBytesPerTick->Observe(static_cast<double>(bytesLastTick));
            }
        }
        _hasSerializationWindow = true;

        const auto start = std::chrono::steady_clock::now();
        _interest.BeginRebuild();
        ForEachEntity([this](NetworkEntity *entity) {
            _interest.Insert(entity);
        });
        if (_replEntities) {
            _replEntities->Set(static_cast<double>(GetReplicaCount()));
        }
        if (_interestRebuildDur) {
            const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            _interestRebuildDur->Observe(secs);
        }
    }

    void ReplicationManager::CollectInterest(NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, std::unordered_set<NetworkEntity *> &out) {
        const size_t before = out.size();
        size_t candidates   = 0;
        _interest.CollectVisible(viewer, viewerGUID, out, &candidates);
        if (_interestCandidates) {
            _interestCandidates->Inc(static_cast<uint64_t>(candidates));
        }
        if (_interestSelected) {
            _interestSelected->Inc(static_cast<uint64_t>(out.size() - before));
        }
    }

    void ReplicationManager::OnClosedConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID, MafiaNet::PI2_LostConnectionReason lostConnectionReason) {
        // The player's avatar is server-created, so the base PopConnection (which only tears down
        // replicas a dropped peer itself created) leaves it behind. Notify the game while the avatar
        // is still resolvable, then destroy it — DestroyEntity broadcasts the destruction to the
        // remaining clients and clears the viewer mapping. Clients keep the base behaviour: their
        // replicas all originate from the server, so PopConnection cleans them up on its own.
        if (_isServer) {
            const auto guid = MafiaNet::ToPeerGuid(rakNetGUID);
            // This plugin callback fires for every closed connection, including peers dropped before
            // they completed identity (build mismatch, quit during the asset phase). Those never
            // produced a player-connect notification, so gate the disconnect notification on the
            // replication connection that PushReplicationConnection creates alongside it — keeping
            // the connect/disconnect callbacks paired for the game.
            if (_onClientDisconnect && GetConnectionByGUID(rakNetGUID) != nullptr) {
                _onClientDisconnect(guid);
            }
            NetworkEntity *viewer = GetViewer(guid);
            // Return any other entity the dropped peer owned (e.g. a vehicle it was driving) to the
            // server, or its authority gate would freeze it against an owner that no longer exists.
            ForEachEntity([&](NetworkEntity *entity) {
                if (entity != viewer && entity->ownerGUID == guid) {
                    SetOwner(entity, MafiaNet::UNASSIGNED_PEER_GUID);
                }
            });
            if (viewer) {
                DestroyEntity(viewer);
            }
        }
        ReplicaManager3::OnClosedConnection(systemAddress, rakNetGUID, lostConnectionReason);
    }

    MafiaNet::Connection_RM3 *ReplicationManager::AllocConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID) const {
        // ReplicaManager3 declares this const, but the connection needs a mutable manager back-pointer
        // for its QueryReplicaList interest queries; the const_cast is forced by the upstream API.
        return new ReplicationConnection(systemAddress, rakNetGUID, const_cast<ReplicationManager *>(this), _isServer);
    }

    void ReplicationManager::DeallocConnection(MafiaNet::Connection_RM3 *connection) const {
        delete connection;
    }
} // namespace Framework::Networking::Replication
