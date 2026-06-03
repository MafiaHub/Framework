/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "network_entity.h"

#include <mafianet/GridSectorizer.h>
#include <mafianet/NetworkIDManager.h>
#include <mafianet/RPC4Plugin.h>
#include <mafianet/ReplicaManager3.h>
#include <mafianet/peerinterface.h>

#include <glm/glm.hpp>
#include <function2.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Framework::Networking::Replication {
    // The replicated world: a ReplicaManager3 that owns the set of NetworkEntity objects. It
    // creates/destroys entities, resolves them by NetworkID, tracks each connection's "viewer"
    // entity, and maintains a GridSectorizer used by ReplicationConnection::QueryReplicaList for
    // interest management.
    class ReplicationManager final : public MafiaNet::ReplicaManager3 {
      public:
        ReplicationManager();

        void Init(MafiaNet::RakPeerInterface *peer, MafiaNet::NetworkIDManager *networkIDManager, MafiaNet::RPC4 *rpc, bool isServer);

        // Server: push the entity's forced state to its owner — the server's authoritative override
        // of an owned entity (see NetworkEntity::ForceState / OnStateForced). No-op for unowned
        // entities, which already replicate to everyone.
        void ForceState(NetworkEntity *entity);

        // Server: change an entity's owner and notify the new owner directly (see
        // NetworkEntity::SetOwner). Needed because serialize to an owner is withheld, so the grant
        // can't ride normal replication.
        void SetOwner(NetworkEntity *entity, uint64_t guid);

        bool IsServer() const {
            return _isServer;
        }
        uint64_t GetMyGUID() const {
            return _myGUID;
        }

        // --- Entity lifecycle ---
        // Server: construct an entity of the given registered type and start replicating it. The
        // caller fills in its state afterwards. Returns nullptr for an unknown type.
        NetworkEntity *CreateEntity(uint32_t typeId);
        // Broadcast destruction and delete the entity.
        void DestroyEntity(NetworkEntity *entity);
        NetworkEntity *GetEntityByNetworkID(MafiaNet::NetworkID networkId) const;
        void ForEachEntity(const fu2::function<void(NetworkEntity *) const> &fn) const;

        // --- Viewers (a connection's controlled entity, e.g. a player's avatar) ---
        void SetViewer(uint64_t guid, NetworkEntity *entity);
        NetworkEntity *GetViewer(uint64_t guid) const;
        void ClearViewer(uint64_t guid);

        // --- Interest management ---
        // Configure the spatial index extent. Defaults cover a 20km² map at 100m cells (~40k cells).
        // Pick bounds that enclose the playable area; entities outside clamp to edge cells (still
        // found by radius queries, just less precisely). Call before the first Tick().
        void ConfigureGrid(float cellSize, float worldMin, float worldMax);
        // Rebuilds the spatial index from current entity positions. Server only; call once per tick
        // before ReplicaManager3 serializes (driven from NetworkPeer::Update).
        void Tick();
        // Appends entities within `radius` of `center` to `out`, de-duplicated. Interest is computed
        // on the XZ ground plane only (vertical separation does not cull) — adjust if your game's
        // ground plane differs.
        void QueryRadius(const glm::vec3 &center, float radius, std::vector<NetworkEntity *> &out);

        // --- ReplicaManager3 factory hooks ---
        MafiaNet::Connection_RM3 *AllocConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID) const override;
        void DeallocConnection(MafiaNet::Connection_RM3 *connection) const override;

      private:
        bool _isServer    = false;
        uint64_t _myGUID  = 0xFFFFFFFFFFFFFFFF;
        // Server-side monotonic NetworkID allocator. Starts at 1 (0 reads as "none" in game code) and
        // stays well within JavaScript's safe-integer range so scripting can hold ids as plain numbers.
        // Bumped only from CreateEntity on the sim thread, so it needs no synchronization.
        uint64_t _nextNetworkId = 0;
        bool _gridReady   = false;
        float _gridCellSize = 100.0f;
        float _gridMin      = -10000.0f;
        float _gridMax      = 10000.0f;
        MafiaNet::RPC4 *_rpc = nullptr;
        GridSectorizer _grid;
        std::unordered_map<uint64_t, NetworkEntity *> _viewers;
    };
} // namespace Framework::Networking::Replication
