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
#include <unordered_set>
#include <utility>
#include <vector>

namespace Framework::Networking::Replication {
    // The replicated world: a ReplicaManager3 that owns the set of NetworkEntity objects. It
    // creates/destroys entities, resolves them by NetworkID, tracks each connection's "viewer"
    // entity, and maintains a GridSectorizer used by ReplicationConnection::QueryReplicaList for
    // interest management.
    class ReplicationManager final : public MafiaNet::ReplicaManager3 {
      public:
        ReplicationManager();
        ~ReplicationManager();

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

        // Interest candidate indices, rebuilt each Tick() from live entities (see QueryReplicaList).
        const std::unordered_set<NetworkEntity *> *EntitiesOwnedBy(uint64_t guid) const;
        const std::unordered_set<NetworkEntity *> &AlwaysVisibleEntities() const {
            return _alwaysVisible;
        }

        // --- Interest management ---
        // Configure the spatial index extent. Defaults cover a 20km² map at 100m cells (~40k cells).
        // Pick bounds that enclose the playable area; entities outside clamp to edge cells (still
        // found by radius queries, just less precisely). Call before the first Tick().
        void ConfigureGrid(float cellSize, float worldMin, float worldMax);
        // Rebuilds the spatial index from current entity positions. Server only; call once per tick
        // before ReplicaManager3 serializes (driven from NetworkPeer::Update).
        void Tick();
        // Inserts entities within `radius` of `center` into `out`. The set both de-duplicates the
        // grid's per-cell hits and gives QueryReplicaList O(1) membership tests. Interest is computed
        // on the XZ ground plane only (vertical separation does not cull) — adjust if your game's
        // ground plane differs.
        void QueryRadius(const glm::vec3 &center, float radius, std::unordered_set<NetworkEntity *> &out);

        // Server: invoked from OnClosedConnection just before the dropped peer's avatar is destroyed,
        // while it is still resolvable. The integration layer wires its player-disconnect notification
        // here.
        void SetOnClientDisconnect(fu2::function<void(uint64_t) const> callback) {
            _onClientDisconnect = std::move(callback);
        }

        // Fired with the NetworkID at the end of CreateEntity / start of DestroyEntity.
        void SetOnEntityCreated(fu2::function<void(uint64_t) const> callback) {
            _onEntityCreated = std::move(callback);
        }
        void SetOnEntityDestroyed(fu2::function<void(uint64_t) const> callback) {
            _onEntityDestroyed = std::move(callback);
        }

        // --- ReplicaManager3 hooks ---
        // Connection-drop teardown. The base only removes replicas a dropped peer itself created;
        // player avatars are server-created, so on the server we additionally notify the game and
        // destroy the dropped peer's viewer (DestroyEntity broadcasts the destruction to remaining
        // clients), which is the missing half that otherwise leaks avatars across reconnects.
        void OnClosedConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID, MafiaNet::PI2_LostConnectionReason lostConnectionReason) override;

        MafiaNet::Connection_RM3 *AllocConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID) const override;
        void DeallocConnection(MafiaNet::Connection_RM3 *connection) const override;

      private:
        bool _isServer    = false;
        uint64_t _myGUID  = MafiaNet::UNASSIGNED_RAKNET_GUID.g;
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
        // Rebuilt from live entities each Tick(); DestroyEntity scrubs them so an intra-tick delete
        // can't dangle.
        std::unordered_map<uint64_t, std::unordered_set<NetworkEntity *>> _ownedByGuid;
        std::unordered_set<NetworkEntity *> _alwaysVisible;
        fu2::function<void(uint64_t) const> _onClientDisconnect;
        fu2::function<void(uint64_t) const> _onEntityCreated;
        fu2::function<void(uint64_t) const> _onEntityDestroyed;
    };
} // namespace Framework::Networking::Replication
