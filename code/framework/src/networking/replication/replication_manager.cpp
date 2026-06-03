/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "replication_manager.h"

#include "entity_factory.h"
#include "replication_connection.h"

#include <mafianet/DS_List.h>

#include <cmath>

namespace Framework::Networking::Replication {
    namespace {
        // Half-extent of a point entity's bounding box in the spatial index. GridSectorizer requires
        // min < max (it asserts otherwise), so a point is inserted as a tiny box around its position.
        constexpr float kPointEpsilon = 0.01f;

        // Built-in server->owner state push (see NetworkEntity::ForceState). Wire: id, then the
        // entity's WriteForcedState payload.
        constexpr const char *kForceStateId = "Framework::ForceState";

        // Built-in server->owner ownership grant (see NetworkEntity::SetOwner). Wire: id, ownerGUID.
        constexpr const char *kSetOwnerId = "Framework::SetOwner";

        // RPC4 hands each slot its registration context back; we pass the owning manager so the
        // handler can resolve entities without a global.
        void OnForceState(MafiaNet::BitStream *bs, MafiaNet::Packet *, void *context) {
            auto *manager = static_cast<ReplicationManager *>(context);
            if (!manager) {
                return;
            }
            MafiaNet::NetworkID networkId;
            bs->Read(networkId);

            auto *entity = manager->GetEntityByNetworkID(networkId);
            if (!entity) {
                return;
            }
            entity->ReadForcedState(bs);
            entity->OnStateForced();
        }

        void OnSetOwner(MafiaNet::BitStream *bs, MafiaNet::Packet *, void *context) {
            auto *manager = static_cast<ReplicationManager *>(context);
            if (!manager) {
                return;
            }
            MafiaNet::NetworkID networkId;
            uint64_t ownerGUID = 0;
            bs->Read(networkId);
            bs->Read(ownerGUID);

            if (auto *entity = manager->GetEntityByNetworkID(networkId)) {
                entity->ownerGUID = ownerGUID;
            }
        }
    } // namespace

    ReplicationManager::ReplicationManager() = default;

    void ReplicationManager::ConfigureGrid(float cellSize, float worldMin, float worldMax) {
        _gridCellSize = cellSize;
        _gridMin      = worldMin;
        _gridMax      = worldMax;
        _gridReady    = false; // re-initialised on next Tick()
    }

    void ReplicationManager::Init(MafiaNet::RakPeerInterface *peer, MafiaNet::NetworkIDManager *networkIDManager, MafiaNet::RPC4 *rpc, bool isServer) {
        _isServer = isServer;
        _myGUID   = peer->GetMyGUID().g;
        _rpc      = rpc;
        SetNetworkIDManager(networkIDManager);
        peer->AttachPlugin(this);

        // The owning client applies forced state pushed by the server (teleports, engine, ...).
        if (_rpc) {
            _rpc->RegisterSlot(kForceStateId, &OnForceState, this, 0);
            _rpc->RegisterSlot(kSetOwnerId, &OnSetOwner, this, 0);
        }
    }

    void ReplicationManager::ForceState(NetworkEntity *entity) {
        if (!entity || !_rpc || entity->ownerGUID == MafiaNet::UNASSIGNED_RAKNET_GUID.g) {
            return;
        }
        MafiaNet::BitStream bs;
        MafiaNet::NetworkID networkId = entity->GetNetworkID();
        bs.Write(networkId);
        entity->WriteForcedState(&bs);
        _rpc->Signal(kForceStateId, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, MafiaNet::RakNetGUID(entity->ownerGUID), false, false);
    }

    void ReplicationManager::SetOwner(NetworkEntity *entity, uint64_t guid) {
        if (!entity) {
            return;
        }
        entity->ownerGUID = guid;
        // The new owner is otherwise blind to the change: the server withholds serialize to whoever
        // owns an entity, so it would never receive the updated ownerGUID. Tell it directly. Other
        // peers, and any previous owner that just lost authority, learn it through normal serialize.
        if (_rpc && _isServer && guid != MafiaNet::UNASSIGNED_RAKNET_GUID.g) {
            MafiaNet::BitStream bs;
            MafiaNet::NetworkID networkId = entity->GetNetworkID();
            bs.Write(networkId);
            bs.Write(guid);
            _rpc->Signal(kSetOwnerId, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, MafiaNet::RakNetGUID(guid), false, false);
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
        return entity;
    }

    void ReplicationManager::DestroyEntity(NetworkEntity *entity) {
        if (!entity) {
            return;
        }
        // Only a viewer entity owns a viewer mapping. Owned non-viewer entities (e.g. a vehicle owned
        // by a player) share the player's GUID, so we must NOT clear the mapping for those.
        if (entity->isViewer && entity->ownerGUID != MafiaNet::UNASSIGNED_RAKNET_GUID.g) {
            ClearViewer(entity->ownerGUID);
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
            entity->isViewer = true;
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

    void ReplicationManager::Tick() {
        if (!_isServer) {
            return;
        }
        if (!_gridReady) {
            _grid.Init(_gridCellSize, _gridCellSize, _gridMin, _gridMin, _gridMax, _gridMax);
            _gridReady = true;
        }

        // GridSectorizer has no incremental removal in the default build, so we rebuild every tick.
        // Entities are inserted as a tiny box around their XZ position (GridSectorizer asserts on a
        // zero-area entry).
        _grid.Clear();
        ForEachEntity([this](NetworkEntity *entity) {
            _grid.AddEntry(entity, entity->position.x - kPointEpsilon, entity->position.z - kPointEpsilon, entity->position.x + kPointEpsilon, entity->position.z + kPointEpsilon);
        });
    }

    void ReplicationManager::QueryRadius(const glm::vec3 &center, float radius, std::unordered_set<NetworkEntity *> &out) {
        if (!_gridReady) {
            return;
        }
        DataStructures::List<void *> hits;
        _grid.GetEntries(hits, center.x - radius, center.z - radius, center.x + radius, center.z + radius);

        const float radiusSq = radius * radius;
        for (unsigned i = 0; i < hits.Size(); ++i) {
            auto *entity = static_cast<NetworkEntity *>(hits[i]);
            if (!entity) {
                continue;
            }
            const glm::vec3 delta = entity->position - center;
            // 2D (XZ) distance check; entries spanning multiple cells can repeat — the set dedupes.
            if (delta.x * delta.x + delta.z * delta.z > radiusSq) {
                continue;
            }
            out.insert(entity);
        }
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
