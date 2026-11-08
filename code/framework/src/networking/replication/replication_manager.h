/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "delegation.h"
#include "entity_registry.h"
#include "interest_grid.h"
#include "network_entity.h"

#include <utils/fork_join_pool.h>

#include <mafianet/network_id_manager.h>
#include <mafianet/peer_interface.h>
#include <mafianet/replica_manager3.h>
#include <mafianet/rpc4_plugin.h>

#include <function2/function2.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Framework::Networking {
    class NetworkPeer;
} // namespace Framework::Networking

namespace Framework::Networking::Replication {
    class ReplicationConnection;

    // Downcast a base NetworkEntity* to a concrete subclass by comparing the registered type id
    // against the compile-time id of T::kTypeName. This is an exact-type match, not an is-a test:
    // it does not see base classes the way dynamic_cast would, and it does not need RTTI, so every
    // target (including the /GR- client) resolves typed lookups the same way.
    template <typename T>
    inline T *CheckedEntityCast(NetworkEntity *entity) {
        static_assert(std::is_base_of_v<NetworkEntity, T>, "CheckedEntityCast<T>: T must derive from NetworkEntity");
        constexpr uint32_t kId = EntityTypeId<T>();
        return (entity != nullptr && entity->GetTypeId() == kId) ? static_cast<T *>(entity) : nullptr;
    }

    // Server-side transform send interval by distance from the viewer: every tick inside nearDistance,
    // midIntervalMs out to midDistance, farIntervalMs beyond. Only the transform channel is throttled;
    // a zero interval means every tick.
    struct SerializeRateBands {
        float nearDistance     = 0.0f;
        float midDistance      = 0.0f;
        uint32_t midIntervalMs = 0;
        uint32_t farIntervalMs = 0;
        // Interval inside nearDistance. Zero, the default, sends every tick: a type whose receivers
        // re-simulate between samples (input replay, interpolation) can take a lower near rate.
        uint32_t nearIntervalMs = 0;

        // Distances are clamped to 0 <= nearDistance <= midDistance, so an inverted or negative band
        // cannot hand a far viewer the near interval.
        SerializeRateBands Normalized() const {
            SerializeRateBands out = *this;
            out.nearDistance       = nearDistance > 0.0f ? nearDistance : 0.0f;
            out.midDistance        = midDistance > out.nearDistance ? midDistance : out.nearDistance;
            return out;
        }
    };

    // The replicated world: a ReplicaManager3 that owns the set of NetworkEntity objects. It
    // creates/destroys entities, resolves them by NetworkID, tracks each connection's "viewer"
    // entity, and drives an InterestGrid that ReplicationConnection::QueryReplicaList reads for
    // interest management.
    // Identifies one state-change subscription. Zero is never handed out.
    using StateChangeHandle                                      = uint32_t;
    inline constexpr StateChangeHandle kInvalidStateChangeHandle = 0;

    class ReplicationManager final: public MafiaNet::ReplicaManager3 {
      public:
        ReplicationManager();
        ~ReplicationManager();

        void Init(NetworkPeer *owner, bool isServer);

        // Sends unreliable transforms as per-connection batches (see TransformBatch). Every receiver
        // reads batches; each game opts its senders in, before connecting. Framework 29 wire protocol.
        void SetTransformBatchingEnabled(bool enabled) {
            _batchTransforms = enabled;
        }
        bool TransformBatchingEnabled() const {
            return _batchTransforms;
        }

        // RakPeer updates every plugin on each Receive(), so draining N packets would run N world
        // passes. Between these two calls only the first Update() runs one; outside them every call
        // does, as in ReplicaManager3.
        void BeginNetworkUpdate() {
            _networkUpdateActive      = true;
            _updatedThisNetworkUpdate = false;
        }
        void EndNetworkUpdate() {
            _networkUpdateActive = false;
        }
        void Update() override;
        // Server: a replica was constructed, seeded with values no pass has compared yet. Makes the
        // pass serialize even if it is not due, so they are what the next delta is taken against;
        // otherwise a value that changed back before then would never reach that viewer.
        void SerializeThisPass();
        // Decodes transform batches and hands everything else to ReplicaManager3.
        MafiaNet::PluginReceiveResult OnReceive(MafiaNet::Packet *packet) override;

        // Server: push the entity's forced state to its owner — the server's authoritative override
        // of an owned entity (see NetworkEntity::ForceState / OnStateForced). Bumps the entity's
        // state epoch so owner updates sent before the override arrives cannot revert it. No-op on
        // clients and for unowned entities (which already replicate to everyone).
        void ForceState(NetworkEntity *entity);

        // Server: change an entity's owner and notify the new owner directly (see
        // NetworkEntity::SetOwner). Needed because serialize to an owner is withheld, so the grant
        // can't ride normal replication.
        void SetOwner(NetworkEntity *entity, MafiaNet::PeerGuid guid);

        // --- State bags ---
        // Server: send this tick's accumulated bag changes. Driven from NetworkPeer::Update, next to
        // RebuildInterest. Each change goes only to the connections that have the entity constructed,
        // so bags inherit interest, virtual worlds and budgets rather than restating them — and no
        // state reaches a client that cannot see the entity.
        void FlushStateBags();

        // Called by a bag when it first dirties in a tick, so the flush walks what changed.
        void MarkStateBagDirty(NetworkEntity *entity);

        // Subscribe to bag changes on this peer: the server on write, a client on apply, including
        // the keys a construction seed delivered. Returns a handle for RemoveStateChangeHandler, or
        // kInvalidStateChangeHandle when the callback is empty.
        //
        // The filter is applied before the callback runs, which is the point of it: a busy server
        // changes bags many times a tick, and a listener watching one key of one entity should not be
        // paying to build arguments for every other change in the world. An empty filter still sees
        // everything, for a mod that wants the firehose.
        StateChangeHandle AddStateChangeHandler(const StateChangeFilter &filter, fu2::function<void(const StateChange &) const> callback);
        void RemoveStateChangeHandler(StateChangeHandle handle);
        void NotifyStateChanged(const StateChange &change);

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

        // Typed wrappers over the base-pointer entity API (cast once inside). CreateEntity<T> resolves
        // the type id from T::kTypeName.
        template <typename T>
        T *CreateEntity() {
            return CheckedEntityCast<T>(CreateEntity(EntityTypeId<T>()));
        }
        template <typename T>
        T *GetEntity(MafiaNet::NetworkID networkId) const {
            return CheckedEntityCast<T>(GetEntityByNetworkID(networkId));
        }
        template <typename T>
        T *GetViewerAs(MafiaNet::PeerGuid guid) const {
            return CheckedEntityCast<T>(GetViewer(guid));
        }
        template <typename T>
        void ForEach(const fu2::function<void(T *) const> &fn) const {
            ForEachEntity([&fn](NetworkEntity *entity) {
                if (T *typed = CheckedEntityCast<T>(entity)) {
                    fn(typed);
                }
            });
        }

        // --- Viewers (a connection's controlled entity, e.g. a player's avatar) ---
        void SetViewer(MafiaNet::PeerGuid guid, NetworkEntity *entity);
        NetworkEntity *GetViewer(MafiaNet::PeerGuid guid) const;
        void ClearViewer(MafiaNet::PeerGuid guid);
        // Every connection that has one, for code that needs "where is each player" rather than one
        // named player: interest is computed per viewer, and so is delegation.
        void ForEachViewer(const fu2::function<void(MafiaNet::PeerGuid, NetworkEntity *) const> &fn) const;
        // Every player's avatar with the guid of the player it stands for, on either peer. What
        // proximity voice keys a speaker's position on.
        void ForEachAvatar(const fu2::function<void(MafiaNet::PeerGuid, NetworkEntity *) const> &fn) const;

        // Server: every peer that has `entity` constructed -- the audience interest, virtual worlds
        // and budgets have already chosen, and so the one an event about the entity belongs to. The
        // same question FlushStateBags asks, answered from the connections rather than re-derived
        // from positions and ranges. O(connections); visits nothing on a client.
        void ForEachStreamingPeer(NetworkEntity *entity, const fu2::function<void(MafiaNet::PeerGuid) const> &fn) const;

        // --- Delegated simulation ---
        // Election of which client simulates which server-owned entity. Server-side; on a client the
        // manager exists but does nothing. See delegation.h.
        DelegationManager &Delegation() {
            return _delegation;
        }
        const DelegationManager &Delegation() const {
            return _delegation;
        }
        // Client: tell the server this client cannot run `entity` right now, or can again. The
        // server passes it over for that entity alone (DelegationManager::SetDeclined).
        void DeclineSimulation(NetworkEntity *entity, bool declined);

        // --- Interest management ---
        // Configure the spatial index extent (see InterestGrid::Configure). Call before the first
        // RebuildInterest().
        void ConfigureGrid(float cellSize, float worldMin, float worldMax);
        // Select the interest ground plane: false (default) = XZ (Y-up), true = XY (Z-up). See
        // InterestGrid::SetGroundPlaneXY.
        void SetInterestGroundPlaneXY(bool groundXY) {
            _interest.SetGroundPlaneXY(groundXY);
            // Delegation measures the same distances for the same reason, so a game that has told
            // the framework which way is up has told it once.
            _delegation.SetGroundPlaneXY(groundXY);
        }
        // Streaming-boundary hysteresis. See InterestGrid::SetStreamOutMargin.
        void SetInterestStreamOutMargin(float margin) {
            _interest.SetStreamOutMargin(margin);
        }
        // Seconds of viewer velocity used as a second focus point. See
        // InterestGrid::SetLookaheadSeconds.
        void SetInterestLookaheadSeconds(float seconds) {
            _interest.SetLookaheadSeconds(seconds);
        }
        // Per-type cap on in-range entities per viewer (0 = uncapped). See InterestGrid::SetBudget.
        void SetInterestBudget(uint32_t typeId, uint32_t maxCount) {
            _interest.SetBudget(typeId, maxCount);
        }
        // By registered type name, so games configure budgets without resolving ids themselves.
        void SetInterestBudget(const std::string &typeName, uint32_t maxCount) {
            _interest.SetBudget(EntityRegistry::Get().TypeId(typeName), maxCount);
        }
        // Holds a type to each entity's own streaming range instead of letting the viewer's widen
        // it. See InterestGrid::SetUsesEntityRange.
        void SetInterestUsesEntityRange(uint32_t typeId, bool usesEntityRange) {
            _interest.SetUsesEntityRange(typeId, usesEntityRange);
        }
        void SetInterestUsesEntityRange(const std::string &typeName, bool usesEntityRange) {
            _interest.SetUsesEntityRange(EntityRegistry::Get().TypeId(typeName), usesEntityRange);
        }
        // How often each viewer's interest is recomputed, in milliseconds of server ticks (0 = every
        // tick, the default). The work is spread evenly: each tick recomputes the share of viewers
        // the tick rate (SetAutoSerializeInterval) gives, round robin, so the cost per tick is the
        // same however long ticks take -- a slow tick does not make the next one slower.
        //
        // Between turns a viewer's set changes only for what concerns it: an entity created or
        // shown to it, one destroyed, a change to its own avatar, ownership or dimension.
        void SetInterestRebuildInterval(uint32_t intervalMs) {
            _interestRebuildInterval = intervalMs;
        }
        // How long the last RebuildInterest and the last replication pass (Update) took, in
        // milliseconds: what the server's tick breakdown reports as replication.
        double LastInterestMs() const {
            return _lastInterestMs;
        }
        double LastPassMs() const {
            return _lastPassMs;
        }
        uint32_t InterestRefreshInterval() const {
            return _interestRebuildInterval;
        }

        // Threads that do replication work alongside the server thread, which waits for them:
        // recomputing viewers' interest and serving connections their share of the serialize pass,
        // each independent of the others. 0 keeps it all on the server thread. Defaults to a few,
        // fewer on a small machine.
        void SetReplicationWorkerThreads(uint32_t workers);

        // The per-tick interest step. Server only; call once per tick before ReplicaManager3
        // serializes (driven from NetworkPeer::Update): brings the spatial index up to date with
        // every entity's position, then computes the interest of the viewers whose turn it is.
        void RebuildInterest();

        // --- Read by ReplicationConnection ---
        const InterestGrid &Interest() const {
            return _interest;
        }
        // Working memory for a query run on the server thread outside RebuildInterest.
        InterestGrid::Scratch &MainScratch() {
            return _scratch.front();
        }
        // Entities every viewer has to look at again, as a sequence each connection reads up to
        // where it got: created, or its audience widened. A connection behind ReevaluationBase has
        // missed some and recomputes.
        uint64_t ReevaluationBase() const {
            return _reevaluationBase;
        }
        uint64_t ReevaluationEnd() const {
            return _reevaluationBase + _reevaluations.size();
        }
        std::pair<uint32_t, uint32_t> ReevaluationAt(uint64_t sequence) const {
            return _reevaluations[static_cast<size_t>(sequence - _reevaluationBase)];
        }

        // Server: who may be sent an entity. The change reaches the viewers it concerns before their
        // next refresh: the players the filter names, or every viewer for the entity alone if `only`
        // is switched on or off. An identical filter concerns nobody.
        void SetViewerFilter(NetworkEntity &entity, ViewerFilter filter);
        // One connection in or out, without copying the filter: false puts it in `except` and takes it
        // off `only`, true undoes both, adding it to `only` while there is one.
        void SetViewerAdmitted(NetworkEntity &entity, MafiaNet::PeerGuid guid, bool admitted);
        // Streams the entity only to this connection, or to everyone in reach if unassigned. Moving it
        // between connections concerns those two; switching it on or off, every viewer, for this
        // entity alone.
        void SetStreamTarget(NetworkEntity &entity, MafiaNet::PeerGuid guid);

        // Server: transform rate bands, default and per-type override.
        void SetSerializeRateBands(const SerializeRateBands &bands) {
            _rateBands = bands.Normalized();
        }
        void SetSerializeRateBands(uint32_t typeId, const SerializeRateBands &bands) {
            _rateBandsByType[typeId] = bands.Normalized();
        }
        void SetSerializeRateBands(const std::string &typeName, const SerializeRateBands &bands) {
            _rateBandsByType[EntityRegistry::Get().TypeId(typeName)] = bands.Normalized();
        }
        const SerializeRateBands &GetSerializeRateBands(uint32_t typeId) const {
            if (_rateBandsByType.empty()) {
                return _rateBands;
            }
            const auto it = _rateBandsByType.find(typeId);
            return it != _rateBandsByType.end() ? it->second : _rateBands;
        }
        bool HasSerializeRateBands() const {
            return _rateBands.midIntervalMs != 0 || _rateBands.farIntervalMs != 0 || !_rateBandsByType.empty();
        }
        // Milliseconds between transform sends at squared distance distSq; 0 = every tick.
        static uint32_t TransformSendIntervalMs(const SerializeRateBands &bands, float distSq);

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
        // Makes the connection of this peer, if any, refresh its interest on its next query.
        void InvalidateInterestOf(MafiaNet::PeerGuid guid);
        // Queues the entity for every viewer to decide on again (see ReevaluationBase).
        void Reevaluate(NetworkEntity *entity);
        // The entity is leaving: out of the index and out of every viewer's set, before its pointer
        // can be handed out again. From DestroyEntity, and from ~NetworkEntity for any other way out.
        void ForgetEntity(NetworkEntity *entity);
        void ForgetSlot(uint32_t slot);
        // Recomputes the viewers RebuildInterest found due, across the worker pool when it pays.
        void ComputeDueInterest();
        // The worker pool, started on first use, for `work` independent items; null when that is too
        // little to share out or workers are off.
        Utils::ForkJoinPool *Workers(size_t work);

        // ReplicaManager3's serialize pass on the same workers.
        struct SerializeExecutor final: MafiaNet::RM3SerializeExecutor {
            ReplicationManager *manager = nullptr;
            void ParallelFor(unsigned int count, void (*fn)(void *context, unsigned int index), void *context) override;
        };
        SerializeExecutor _serializeExecutor;
        friend class NetworkEntity;

        bool _isServer                 = false;
        bool _batchTransforms          = false;
        bool _networkUpdateActive      = false;
        bool _serializeForced          = false;
        bool _updatedThisNetworkUpdate = false;
        MafiaNet::PeerGuid _myGUID     = MafiaNet::UNASSIGNED_PEER_GUID;
        // Server-side monotonic NetworkID allocator. Starts at 1 (0 reads as "none" in game code) and
        // stays well within JavaScript's safe-integer range so scripting can hold ids as plain numbers.
        // Bumped only from CreateEntity on the sim thread, so it needs no synchronization.
        uint64_t _nextNetworkId    = 0;
        NetworkPeer *_owner        = nullptr;
        bool _clientRPCsRegistered = false;
        bool _serverRPCsRegistered = false;
        InterestGrid _interest;
        DelegationManager _delegation;
        SerializeRateBands _rateBands;
        std::unordered_map<uint32_t, SerializeRateBands> _rateBandsByType;
        uint32_t _interestRebuildInterval = 0;
        // Viewers owed a recompute, in fractions: each tick adds its share, whole ones are spent.
        double _refreshCredit   = 0.0;
        uint32_t _refreshCursor = 0;
        std::vector<std::pair<uint32_t, uint32_t>> _reevaluations; // slot, generation
        uint64_t _reevaluationBase = 0;
        std::vector<ReplicationConnection *> _dueConnections;
        // One scratch per lane of the pool; the first is the server thread's.
        std::vector<InterestGrid::Scratch> _scratch = std::vector<InterestGrid::Scratch>(1);
        std::unique_ptr<Utils::ForkJoinPool> _workers;
        uint32_t _workerThreads = 0;
        bool _workersSet        = false;
        double _lastInterestMs  = 0.0;
        double _lastPassMs      = 0.0;
        std::unordered_map<MafiaNet::PeerGuid, NetworkEntity *> _viewers;
        // By NetworkID, not pointer: an entity can be destroyed between dirtying and the flush.
        std::unordered_set<uint64_t> _dirtyStateBags;

        struct StateChangeSubscription {
            StateChangeFilter filter;
            fu2::function<void(const StateChange &) const> callback;
        };
        // Handles rather than iterators or pointers, so a handler that unsubscribes during dispatch
        // (itself or another) cannot invalidate what the dispatch is walking.
        std::unordered_map<StateChangeHandle, StateChangeSubscription> _stateChangeHandlers;
        // Keyed buckets, so a change consults only the subscriptions that named its key plus the ones
        // that named no key at all.
        std::unordered_map<std::string, std::vector<StateChangeHandle>> _stateChangeByKey;
        std::vector<StateChangeHandle> _stateChangeAnyKey;
        StateChangeHandle _nextStateChangeHandle = kInvalidStateChangeHandle;
        fu2::function<void(MafiaNet::PeerGuid) const> _onClientDisconnect;
        fu2::function<void(uint64_t) const> _onEntityCreated;
        fu2::function<void(uint64_t) const> _onEntityDestroyed;
    };
} // namespace Framework::Networking::Replication
