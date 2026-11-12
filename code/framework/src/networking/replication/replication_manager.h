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
#include "world.h"

#include <utils/fork_join_pool.h>

#include <mafianet/network_id_manager.h>
#include <mafianet/peer_interface.h>
#include <mafianet/replica_manager3.h>
#include <mafianet/rpc4_plugin.h>

#include <function2/function2.hpp>
#include <glm/glm.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
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
    struct TestAccess;

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
    //
    // Server threading. Entities are reached through GetWorld(), whose handles hold their locks.
    // What this class does on an entity's behalf is safe from any thread, in one of two ways:
    //  - what takes an entity by handle (or is called on one, like NetworkEntity::SetOwner) works on
    //    that entity and otherwise only queues or sends, so it is safe inside the handle's scope;
    //  - what takes a network id takes the handle itself, and what walks connections takes
    //    ReplicaManager3's world: neither may be called from inside a handle's scope, and both
    //    abort if they are.
    // Locks are taken in this order: ReplicaManager3's world, a connection, registered states in
    // registration order, entities by ascending id, then this class's own mutex.
    // Identifies one state-change subscription. Zero is never handed out.
    using StateChangeHandle                                      = uint32_t;
    inline constexpr StateChangeHandle kInvalidStateChangeHandle = 0;

    // ReplicaManager3 is a private base: its connection and replica lists hand out entities by
    // pointer, which only this class, the world and the connections may touch. What a server needs
    // of it is named below.
    class ReplicationManager final: private MafiaNet::ReplicaManager3 {
      public:
        using MafiaNet::ReplicaManager3::Clear;
        using MafiaNet::ReplicaManager3::GetConnectionCount;
        using MafiaNet::ReplicaManager3::SetAutoManageConnections;
        using MafiaNet::ReplicaManager3::SetAutoSerializeInterval;
        // Whether replication to this peer has started, and starting it (idempotent). See
        // NetworkServer::PushReplicationConnection.
        bool HasConnection(MafiaNet::RakNetGUID guid) const {
            return GetConnectionByGUID(guid) != nullptr;
        }
        void StartReplication(const MafiaNet::SystemAddress &address, MafiaNet::RakNetGUID guid);

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

        // Server: push the entity's forced state to its owner — the server's authoritative override
        // of an owned entity (see NetworkEntity::ForceState / OnStateForced). Bumps the entity's
        // state epoch so owner updates sent before the override arrives cannot revert it. No-op on
        // clients and for unowned entities (which already replicate to everyone).
        void ForceState(uint64_t networkId);
        template <typename T>
        void ForceState(const Ref<T> &entity) {
            if (entity) {
                ForceState(*entity);
            }
        }

        // Server: change an entity's owner and notify the new owner directly (see
        // NetworkEntity::SetOwner). Needed because serialize to an owner is withheld, so the grant
        // can't ride normal replication.
        void SetOwner(uint64_t networkId, MafiaNet::PeerGuid guid);
        template <typename T>
        void SetOwner(const Ref<T> &entity, MafiaNet::PeerGuid guid) {
            if (entity) {
                SetOwner(*entity, guid);
            }
        }

        // Server: move an entity to another virtual world (dimension).
        void SetVirtualWorld(uint64_t networkId, MafiaNet::VirtualWorldId virtualWorld);
        template <typename T>
        void SetVirtualWorld(const Ref<T> &entity, MafiaNet::VirtualWorldId virtualWorld) {
            if (entity) {
                entity->SetVirtualWorld(virtualWorld);
            }
        }

        // --- State bags ---
        // Server: send this tick's accumulated bag changes. Driven from NetworkPeer::Update, next to
        // RebuildInterest. Each change goes only to the connections that have the entity constructed,
        // so bags inherit interest, virtual worlds and budgets rather than restating them — and no
        // state reaches a client that cannot see the entity.
        void FlushStateBags();

        // Called by a bag when it first dirties in a tick, so the flush walks what changed.
        void MarkStateBagDirty(uint64_t networkId);

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

        // --- Entities ---
        // Server: entities and registered state behind handles that lock them (world.h): how they
        // are created, reached, visited and destroyed. A client reaches its own by pointer, through
        // ClientEntities (client_entities.h).
        World &GetWorld() {
            return _world;
        }

        // --- Viewers (a connection's controlled entity, e.g. a player's avatar) ---
        // The map is kept by network id and is safe to ask from any thread. By id, the entity that
        // stops being this connection's viewer loses its flag at once; by handle, at the next tick.
        void SetViewer(MafiaNet::PeerGuid guid, uint64_t networkId);
        template <typename T>
        void SetViewer(MafiaNet::PeerGuid guid, const Ref<T> &entity) {
            if (entity) {
                SetViewer(guid, *entity);
            }
        }
        // Network id of the connection's viewer, 0 when it has none.
        uint64_t GetViewerId(MafiaNet::PeerGuid guid) const;
        // Every connection that has a viewer, with its network id.
        std::vector<std::pair<MafiaNet::PeerGuid, uint64_t>> GetViewerIds() const;
        void ClearViewer(MafiaNet::PeerGuid guid);

        // Server: every peer that has `entity` constructed -- the audience interest, virtual worlds
        // and budgets have already chosen, and so the one an event about the entity belongs to. The
        // same question FlushStateBags asks, answered from the connections rather than re-derived
        // from positions and ranges. O(connections); visits nothing on a client. Walks the
        // connections, so not from inside a handle's scope.
        void ForEachStreamingPeer(uint64_t networkId, const fu2::function<void(MafiaNet::PeerGuid) const> &fn);

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
        // Client: an entity the server constructed here, made known to the world.
        void AdoptRemote(NetworkEntity *entity) {
            _world.Adopt(entity);
        }

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
        // The same work on a pool the caller owns and shares with its own per-tick loops, which must
        // outlive this manager's use of it. Null goes back to the manager's own workers.
        void SetReplicationWorkerPool(Utils::ForkJoinPool *pool);

        // Connections whose construct and destroy lists the last pass took as worked out with their
        // interest, instead of deciding them again itself.
        uint32_t PreparedDiffsLastPass() const {
            return _preparedDiffs;
        }
        void NotePreparedDiff() {
            ++_preparedDiffs;
        }

        // The per-tick interest step. Server only; call once per tick before ReplicaManager3
        // serializes (driven from NetworkPeer::Update): brings the spatial index up to date with
        // every entity's position, then computes the interest of the viewers whose turn it is.
        void RebuildInterest();

        // --- Read by ReplicationConnection ---
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

        // Fired with the NetworkID at the end of CreateEntity, before the world resolves the id, and
        // when the entity is torn down (see World::Destroy). The creation callback runs with the new
        // entity locked for its creator: it must not take ReplicaManager3's world.
        void SetOnEntityCreated(fu2::function<void(uint64_t) const> callback) {
            _onEntityCreated = std::move(callback);
        }
        void SetOnEntityDestroyed(fu2::function<void(uint64_t) const> callback) {
            _onEntityDestroyed = std::move(callback);
        }

      private:
        // --- ReplicaManager3 hooks ---
        // Decodes transform batches and hands everything else to ReplicaManager3.
        MafiaNet::PluginReceiveResult OnReceive(MafiaNet::Packet *packet) override;
        // Connection-drop teardown. The base only removes replicas a dropped peer itself created;
        // player avatars are server-created, so on the server we additionally notify the game and
        // destroy the dropped peer's viewer (which broadcasts the destruction to remaining
        // clients), which is the missing half that otherwise leaks avatars across reconnects.
        void OnClosedConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID, MafiaNet::PI2_LostConnectionReason lostConnectionReason) override;

        MafiaNet::Connection_RM3 *AllocConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID) const override;
        void DeallocConnection(MafiaNet::Connection_RM3 *connection) const override;

        // The tests drive ReplicaManager3 directly: connections without a network behind them.
        friend struct TestAccess;

        // The interest index, for the connections: its slots give entities by pointer.
        const InterestGrid &Interest() const {
            return _interest;
        }
        // Working memory for a query run on the server thread outside RebuildInterest.
        InterestGrid::Scratch &MainScratch() {
            return _scratch.front();
        }

        // --- Entities by pointer: this class's own, the world's, and a client's (ClientEntities) ---
        // Server: construct and start replicating an entity of the given type, nullptr if unknown.
        // It is locked for its creator, as `hold`, from before any other thread can find it.
        NetworkEntity *CreateEntity(uint32_t typeId, Detail::WorldHold &hold);
        // Reads the NetworkIDManager's table under no lock of ReplicaManager3: on a server the
        // caller holds its world, or the entity's handle, for as long as it uses the pointer.
        NetworkEntity *GetEntityByNetworkID(MafiaNet::NetworkID networkId) const;
        void ForEachEntity(const fu2::function<void(NetworkEntity *) const> &fn) const;
        NetworkEntity *GetViewer(MafiaNet::PeerGuid guid) const;
        friend class ClientEntities;

        // The effects above on an entity the caller holds.
        void ForceState(NetworkEntity &entity);
        void SetOwner(NetworkEntity &entity, MafiaNet::PeerGuid guid);
        void SetViewer(MafiaNet::PeerGuid guid, NetworkEntity &entity);

        // What an effect on a held entity owes the interest index and the connections, which
        // ReplicaManager3's world protects and a handle's scope cannot take: queued here and applied
        // by the tick before it next looks at either.
        struct Pending {
            enum class Kind : uint8_t {
                Resync,     // the index takes its view of the entity afresh: its owner or audience changed
                Reevaluate, // every viewer decides on the entity again
                Invalidate, // the connection of `guid` recomputes its interest
                Unview      // the entity may have stopped being a viewer
            };
            Kind kind;
            uint64_t id             = 0;
            MafiaNet::PeerGuid guid = MafiaNet::UNASSIGNED_PEER_GUID;
        };
        void Queue(Pending pending);
        void ApplyPending();

        // Makes the connection of this peer, if any, refresh its interest on its next query.
        void InvalidateInterestOf(MafiaNet::PeerGuid guid);
        // Queues the entity for every viewer to decide on again (see ReevaluationBase).
        void Reevaluate(NetworkEntity *entity);
        // Takes the index's view of one entity afresh, between two syncs of them all. The caller
        // holds the entity, and ReplicaManager3's world.
        void SyncInterestOf(NetworkEntity *entity) {
            if (!entity->_leaving) {
                _interest.Sync(entity);
            }
        }
        // The entity is leaving: out of the index and out of every viewer's set, before its pointer
        // can be handed out again. From its teardown, and from ~NetworkEntity for any other way out.
        void ForgetEntity(NetworkEntity *entity);
        void ForgetSlot(uint32_t slot);
        // Server: the owner's destruction of its entity, already accepted. True once the entity is
        // gone; false when it was on its way out already, or waits on a handle.
        bool DestroyByOwner(NetworkEntity *entity, MafiaNet::Connection_RM3 *source);
        // What World::Destroy ends in, with nothing left holding the entity: out of the viewer map,
        // the index and delegation, then the destruction broadcast and the delete.
        void TeardownEntity(NetworkEntity *entity);
        // Destroyed through the world and waiting on a handle to be torn down.
        bool DestroyPending(NetworkEntity *entity) const {
            return _world.AnyDoomed() && _world.IsDoomed(entity->GetNetworkID());
        }
        bool DestroyPending(uint64_t networkId) const {
            return _world.IsDoomed(networkId);
        }
        // ForEachEntity with those included: the interest index keeps them until they are torn down.
        void ForEachReferenced(const fu2::function<void(NetworkEntity *) const> &fn) const;
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
        friend class World;
        friend class DelegationManager;
        friend class ReplicationConnection;

        bool _isServer                 = false;
        bool _batchTransforms          = false;
        bool _networkUpdateActive      = false;
        bool _serializeForced          = false;
        bool _updatedThisNetworkUpdate = false;
        MafiaNet::PeerGuid _myGUID     = MafiaNet::UNASSIGNED_PEER_GUID;
        // Server-side monotonic NetworkID allocator. Starts at 1 (0 reads as "none" in game code) and
        // stays well within JavaScript's safe-integer range so scripting can hold ids as plain numbers.
        std::atomic<uint64_t> _nextNetworkId {0};
        NetworkPeer *_owner        = nullptr;
        bool _clientRPCsRegistered = false;
        InterestGrid _interest;
        World _world {*this};
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
        Utils::ForkJoinPool *_sharedWorkers = nullptr; // not owned
        uint32_t _preparedDiffs             = 0;
        uint32_t _workerThreads = 0;
        bool _workersSet        = false;
        double _lastInterestMs  = 0.0;
        double _lastPassMs      = 0.0;
        // Guards the viewer map, the dirty bags, the queue and the state-change subscriptions. Taken
        // last and never held across a call out of this class.
        mutable std::mutex _mutex;
        std::unordered_map<MafiaNet::PeerGuid, uint64_t> _viewers;
        std::vector<Pending> _pending;
        std::atomic<bool> _anyPending {false};
        // By NetworkID, not pointer: an entity can be destroyed between dirtying and the flush.
        std::unordered_set<uint64_t> _dirtyStateBags;
        // FlushStateBags scratch, kept between flushes to reuse their capacity: the changes built once
        // per flush, and per connection the indices of the ones it is sent.
        // Defined in the .cpp: this header must not pull in a header from networking/rpc. On Windows the
        // SDK's own #include "rpc.h" would then resolve to ours, MSVC searching the including files' folders.
        struct StateBagFlushPool;
        std::unique_ptr<StateBagFlushPool> _stateBagFlushPool;
        std::vector<std::vector<uint32_t>> _stateBagFlushIndices;

        struct StateChangeSubscription {
            StateChangeFilter filter;
            // Shared, so a dispatch calls it outside the mutex while another thread unsubscribes.
            std::shared_ptr<const fu2::function<void(const StateChange &) const>> callback;
        };
        void DispatchStateChange(const StateChange &change);
        // Handles rather than iterators or pointers, so a handler that unsubscribes during dispatch
        // (itself or another) cannot invalidate what the dispatch is walking.
        std::unordered_map<StateChangeHandle, StateChangeSubscription> _stateChangeHandlers;
        // Keyed buckets, so a change consults only the subscriptions that named its key plus the ones
        // that named no key at all.
        std::unordered_map<std::string, std::vector<StateChangeHandle>> _stateChangeByKey;
        std::vector<StateChangeHandle> _stateChangeAnyKey;
        StateChangeHandle _nextStateChangeHandle = kInvalidStateChangeHandle;
        bool _anyStateChangeHandlers             = false;
        fu2::function<void(MafiaNet::PeerGuid) const> _onClientDisconnect;
        fu2::function<void(uint64_t) const> _onEntityCreated;
        fu2::function<void(uint64_t) const> _onEntityDestroyed;
    };
} // namespace Framework::Networking::Replication
