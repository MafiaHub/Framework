/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "interest_grid.h"
#include "network_entity.h"

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

namespace Framework::Networking {
    class NetworkPeer;
} // namespace Framework::Networking

namespace Framework::Networking::Replication {
    // The replicated world: a ReplicaManager3 that owns the set of NetworkEntity objects. It
    // creates/destroys entities, resolves them by NetworkID, tracks each connection's "viewer"
    // entity, and drives an InterestGrid that ReplicationConnection::QueryReplicaList reads for
    // interest management.
    class ReplicationManager final : public MafiaNet::ReplicaManager3 {
      public:
        ReplicationManager();
        ~ReplicationManager();

        void Init(NetworkPeer *owner, bool isServer);

        // Server: push the entity's forced state to its owner — the server's authoritative override
        // of an owned entity (see NetworkEntity::ForceState / OnStateForced). No-op for unowned
        // entities, which already replicate to everyone.
        void ForceState(NetworkEntity *entity);

        // Server: change an entity's owner and notify the new owner directly (see
        // NetworkEntity::SetOwner). Needed because serialize to an owner is withheld, so the grant
        // can't ride normal replication.
        void SetOwner(NetworkEntity *entity, MafiaNet::PeerGuid guid);

        bool IsServer() const {
            return _isServer;
        }
        MafiaNet::PeerGuid GetMyGUID() const {
            return _myGUID;
        }

        // --- Entity lifecycle ---
        // Server: construct and start replicating an entity of the given type, nullptr if unknown.
        // Non-owning: the manager owns it; destroy via DestroyEntity.
        NetworkEntity *CreateEntity(uint32_t typeId);
        // Broadcast destruction and delete the entity.
        void DestroyEntity(NetworkEntity *entity);
        NetworkEntity *GetEntityByNetworkID(MafiaNet::NetworkID networkId) const;
        void ForEachEntity(const fu2::function<void(NetworkEntity *) const> &fn) const;

        // --- Viewers (a connection's controlled entity, e.g. a player's avatar) ---
        void SetViewer(MafiaNet::PeerGuid guid, NetworkEntity *entity);
        NetworkEntity *GetViewer(MafiaNet::PeerGuid guid) const;
        void ClearViewer(MafiaNet::PeerGuid guid);

        // --- Interest management ---
        // Configure the spatial index extent (see InterestGrid::Configure). Call before the first
        // RebuildInterest().
        void ConfigureGrid(float cellSize, float worldMin, float worldMax);
        // Rebuild the spatial index from current entity positions. Server only; call once per tick
        // before ReplicaManager3 serializes (driven from NetworkPeer::Update).
        void RebuildInterest();
        void CollectInterest(NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, std::unordered_set<NetworkEntity *> &out);
        // Change counter for the interest index (see InterestGrid::Generation).
        uint32_t InterestGeneration() const {
            return _interest.Generation();
        }

        // Server: invoked from OnClosedConnection just before the dropped peer's avatar is destroyed,
        // while it is still resolvable. The integration layer wires its player-disconnect notification
        // here.
        void SetOnClientDisconnect(fu2::function<void(MafiaNet::PeerGuid) const> callback) {
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
        MafiaNet::PeerGuid _myGUID  = MafiaNet::UNASSIGNED_PEER_GUID;
        // Server-side monotonic NetworkID allocator. Starts at 1 (0 reads as "none" in game code) and
        // stays well within JavaScript's safe-integer range so scripting can hold ids as plain numbers.
        // Bumped only from CreateEntity on the sim thread, so it needs no synchronization.
        uint64_t _nextNetworkId = 0;
        NetworkPeer *_owner = nullptr;
        bool _clientRPCsRegistered = false;
        InterestGrid _interest;
        std::unordered_map<MafiaNet::PeerGuid, NetworkEntity *> _viewers;
        fu2::function<void(MafiaNet::PeerGuid) const> _onClientDisconnect;
        fu2::function<void(uint64_t) const> _onEntityCreated;
        fu2::function<void(uint64_t) const> _onEntityDestroyed;
    };
} // namespace Framework::Networking::Replication
