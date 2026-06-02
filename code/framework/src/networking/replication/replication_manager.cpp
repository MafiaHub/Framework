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

#include <algorithm>
#include <cmath>

namespace Framework::Networking::Replication {
    namespace {
        // Half-extent of a point entity's bounding box in the spatial index. GridSectorizer requires
        // min < max (it asserts otherwise), so a point is inserted as a tiny box around its position.
        constexpr float kPointEpsilon = 0.01f;

        // Built-in server->owner state push (see NetworkEntity::ForceState). Wire: id, then the
        // entity's WriteForcedState payload.
        constexpr const char *kForceStateId = "Framework::ForceState";

        // The local manager, so the (non-capturing) RPC handler can resolve entities. One per peer.
        ReplicationManager *g_manager = nullptr;

        void OnForceState(MafiaNet::BitStream *bs, MafiaNet::Packet *) {
            if (!g_manager) {
                return;
            }
            MafiaNet::NetworkID networkId;
            bs->Read(networkId);

            auto *entity = g_manager->GetEntityByNetworkID(networkId);
            if (!entity) {
                return;
            }
            entity->ReadForcedState(bs);
            entity->OnStateForced();
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
        g_manager = this;
        if (_rpc) {
            // Slot, not function: ForceState is delivered with Signal() (see ForceState below).
            _rpc->RegisterSlot(kForceStateId, &OnForceState, 0);
        }
    }

    void ReplicationManager::ForceState(NetworkEntity *entity) {
        if (!entity || !_rpc || entity->ownerGUID == 0xFFFFFFFFFFFFFFFF) {
            return;
        }
        MafiaNet::BitStream bs;
        MafiaNet::NetworkID networkId = entity->GetNetworkID();
        bs.Write(networkId);
        entity->WriteForcedState(&bs);
        _rpc->Signal(kForceStateId, &bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, MafiaNet::RakNetGUID(entity->ownerGUID), false, false);
    }

    NetworkEntity *ReplicationManager::CreateEntity(uint32_t typeId) {
        NetworkEntity *entity = EntityFactory::Get().Create(typeId);
        if (!entity) {
            return nullptr;
        }
        // Assign a small, sequential id BEFORE Reference(): the NetworkIDManager then tracks the
        // entity under this id instead of minting a random 64-bit one (its peer-to-peer scheme). Those
        // random ids overflow JavaScript's 2^53 exact-integer range, so a script reading an entity id
        // gets a rounded value that no longer resolves — the same reason MTA:SA keeps element ids
        // small. Clients adopt this id via the construction snapshot, so both sides agree.
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
        if (entity->isViewer && entity->ownerGUID != 0xFFFFFFFFFFFFFFFF) {
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
        const unsigned count = const_cast<ReplicationManager *>(this)->GetReplicaCount();
        for (unsigned i = 0; i < count; ++i) {
            auto *entity = static_cast<NetworkEntity *>(const_cast<ReplicationManager *>(this)->GetReplicaAtIndex(i));
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

    void ReplicationManager::QueryRadius(const glm::vec3 &center, float radius, std::vector<NetworkEntity *> &out) {
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
            // 2D (XZ) distance check; entries spanning multiple cells can repeat, so skip dupes.
            if (delta.x * delta.x + delta.z * delta.z > radiusSq) {
                continue;
            }
            if (std::find(out.begin(), out.end(), entity) == out.end()) {
                out.push_back(entity);
            }
        }
    }

    MafiaNet::Connection_RM3 *ReplicationManager::AllocConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID) const {
        return new ReplicationConnection(systemAddress, rakNetGUID, const_cast<ReplicationManager *>(this), _isServer);
    }

    void ReplicationManager::DeallocConnection(MafiaNet::Connection_RM3 *connection) const {
        delete connection;
    }
} // namespace Framework::Networking::Replication
