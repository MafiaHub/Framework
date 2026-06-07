/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "replication_manager.h"

#include "../network_peer.h"
#include "entity_factory.h"
#include "replication_connection.h"

namespace Framework::Networking::Replication {
    namespace {
        // Built-in server->owner state push (see NetworkEntity::ForceState). The tail is the entity's
        // SerializeForcedState payload, which is polymorphic, so this rides the raw RPC path: the
        // handler reads the id, resolves the entity, then lets it deserialize the rest.
        constexpr const char *kForceStateId = "Framework::ForceState";

        // Built-in server->owner ownership grant (see NetworkEntity::SetOwner). Fixed shape, so it
        // uses the typed RPC path.
        struct SetOwnerRPC {
            static constexpr const char *kIdentifier = "Framework::SetOwner";

            MafiaNet::NetworkID networkId;
            uint64_t ownerGUID = 0;

            void Serialize(MafiaNet::BitStream *bs, bool write) {
                bs->SerializeCompressed(write, networkId);
                bs->Serialize(write, ownerGUID);
            }
        };
    } // namespace

    ReplicationManager::ReplicationManager() = default;

    // The ForceState/SetOwner handlers are owned by the peer (RegisterRawRPC / RegisterRPC) and torn
    // down with it; the manager shares the peer's lifetime, so no manual unregister is needed.
    ReplicationManager::~ReplicationManager() = default;

    void ReplicationManager::ConfigureGrid(float cellSize, float worldMin, float worldMax) {
        _interest.Configure(cellSize, worldMin, worldMax);
    }

    void ReplicationManager::Init(NetworkPeer *owner, bool isServer) {
        _owner    = owner;
        _isServer = isServer;
        _myGUID   = owner->GetPeer()->GetMyGUID().g;
        SetNetworkIDManager(owner->GetNetworkIDManager());
        owner->GetPeer()->AttachPlugin(this);

        // ForceState/SetOwner are strictly server->owner pushes: the server is always the sender and
        // never a legitimate receiver. Register the handlers on the client only, so a peer cannot
        // signal them back at the server to teleport entities or reassign ownership it doesn't hold.
        if (!_isServer) {
            owner->RegisterRawRPC(kForceStateId, [this](MafiaNet::BitStream *bs, MafiaNet::Packet *) {
                MafiaNet::NetworkID networkId;
                bs->ReadCompressed(networkId);
                if (auto *entity = GetEntityByNetworkID(networkId)) {
                    entity->SerializeForcedState(bs, false);
                    entity->OnStateForced();
                }
            });
            owner->RegisterRPC<SetOwnerRPC>([this](const SetOwnerRPC &payload, MafiaNet::Packet *) {
                if (auto *entity = GetEntityByNetworkID(payload.networkId)) {
                    entity->ownerGUID = payload.ownerGUID;
                }
            });
        }
    }

    void ReplicationManager::ForceState(NetworkEntity *entity) {
        if (!entity || !_owner || entity->ownerGUID == MafiaNet::UNASSIGNED_RAKNET_GUID.g) {
            return;
        }
        MafiaNet::BitStream bs;
        MafiaNet::NetworkID networkId = entity->GetNetworkID();
        // NetworkIDs are small and monotonic, so WriteCompressed strips the leading zero bytes.
        bs.WriteCompressed(networkId);
        entity->SerializeForcedState(&bs, true);
        _owner->SendRawRPC(kForceStateId, bs, MafiaNet::RakNetGUID(entity->ownerGUID));
    }

    void ReplicationManager::SetOwner(NetworkEntity *entity, uint64_t guid) {
        if (!entity) {
            return;
        }
        entity->ownerGUID = guid;
        // Serialize to an owner is withheld, so the grant can't ride normal replication: tell the new
        // owner directly. Other peers (and any prior owner) pick it up through serialize.
        if (_owner && _isServer && guid != MafiaNet::UNASSIGNED_RAKNET_GUID.g) {
            SetOwnerRPC payload;
            payload.networkId = entity->GetNetworkID();
            payload.ownerGUID = guid;
            _owner->SendRPC(payload, MafiaNet::RakNetGUID(guid));
        }
    }

    NetworkEntity *ReplicationManager::CreateEntity(uint32_t typeId) {
        NetworkEntity *entity = EntityFactory::Get().Create(typeId);
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
        return entity;
    }

    void ReplicationManager::DestroyEntity(NetworkEntity *entity) {
        if (!entity) {
            return;
        }
        // Only a viewer entity owns a viewer mapping; owned non-viewer entities share the owner GUID.
        if (entity->streaming.isViewer && entity->ownerGUID != MafiaNet::UNASSIGNED_RAKNET_GUID.g) {
            ClearViewer(entity->ownerGUID);
        }
        // Scrub the interest indices so this delete can't dangle before the next rebuild.
        _interest.Remove(entity);
        if (_onEntityDestroyed) {
            _onEntityDestroyed(entity->GetNetworkID());
        }
        // BroadcastDestruction must precede deletion; ~Replica3 dereferences automatically.
        entity->BroadcastDestruction();
        delete entity;
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

    void ReplicationManager::SetViewer(uint64_t guid, NetworkEntity *entity) {
        if (entity) {
            entity->streaming.isViewer = true;
        }
        _viewers[guid] = entity;
    }

    NetworkEntity *ReplicationManager::GetViewer(uint64_t guid) const {
        const auto it = _viewers.find(guid);
        return it != _viewers.end() ? it->second : nullptr;
    }

    void ReplicationManager::ClearViewer(uint64_t guid) {
        _viewers.erase(guid);
    }

    void ReplicationManager::RebuildInterest() {
        if (!_isServer) {
            return;
        }
        _interest.BeginRebuild();
        ForEachEntity([this](NetworkEntity *entity) {
            _interest.Insert(entity);
        });
    }

    void ReplicationManager::CollectInterest(NetworkEntity *viewer, uint64_t viewerGUID, std::unordered_set<NetworkEntity *> &out) {
        _interest.CollectVisible(viewer, viewerGUID, out);
    }

    void ReplicationManager::OnClosedConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID, MafiaNet::PI2_LostConnectionReason lostConnectionReason) {
        // The player's avatar is server-created, so the base PopConnection (which only tears down
        // replicas a dropped peer itself created) leaves it behind. Notify the game while the avatar
        // is still resolvable, then destroy it — DestroyEntity broadcasts the destruction to the
        // remaining clients and clears the viewer mapping. Clients keep the base behaviour: their
        // replicas all originate from the server, so PopConnection cleans them up on its own.
        if (_isServer) {
            if (_onClientDisconnect) {
                _onClientDisconnect(rakNetGUID.g);
            }
            if (auto *viewer = GetViewer(rakNetGUID.g)) {
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
