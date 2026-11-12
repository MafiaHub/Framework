/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "replication_manager.h"

#include "../channels.h"
#include "../network_peer.h"
#include "../rpc/decline_simulation.h"
#include "../rpc/rpc_identifier.h"
#include "../rpc/state_bag_sync.h"
#include "entity_registry.h"
#include "replication_connection.h"

#include <utils/profiler.h>
#include <utils/thread_budget.h>
#include <utils/time.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <thread>

namespace Framework::Networking::Replication {
    namespace {
        // Whether a ReplicaManager3 construction message lists at least one construction (it may
        // carry destructions alone): [ID_TIMESTAMP, time,] id, world, uint16 count, ...
        bool CarriesConstructions(const MafiaNet::Packet *packet) {
            std::size_t offset = 2;
            unsigned char id   = packet->length > 0 ? packet->data[0] : 0;
            if (id == ID_TIMESTAMP) {
                offset = 3 + sizeof(MafiaNet::Time);
                id     = packet->length > 1 + sizeof(MafiaNet::Time) ? packet->data[1 + sizeof(MafiaNet::Time)] : 0;
            }
            if (id != ID_REPLICA_MANAGER_CONSTRUCTION || packet->length < offset + sizeof(uint16_t)) {
                return false;
            }
            MafiaNet::BitStream bs(packet->data + offset, packet->length - offset, false);
            uint16_t count = 0;
            return bs.Read(count) && count != 0;
        }

        // StateBagSync as the flush sends it: indices into changes built once, instead of a copy
        // of each change per connection. Same identifier, same bytes.
        struct StateBagSyncView {
            static constexpr auto kIdentifier                  = RPC::StateBagSync::kIdentifier;
            const std::vector<RPC::StateBagSync::Change> *pool = nullptr;
            const uint32_t *indices                            = nullptr;
            uint16_t count                                     = 0;

            void Serialize(MafiaNet::BitStream *bs, bool /*write*/) {
                RPC::StateBagSync::WriteChanges(bs, *pool, indices, count);
            }
        };

        // Raw RPC: the tail is the entity's polymorphic SerializeForcedState payload.
        constexpr auto kForceStateId = FW_RPC_IDENTIFIER("Framework::ForceState");

        struct SetOwnerRPC {
            static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("Framework::SetOwner");

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

    namespace {
        // Below this many viewers in a tick, waking the workers costs more than it saves.
        constexpr size_t kParallelMin = 8;

        // Writes the milliseconds from construction to destruction into `into`, however the scope ends.
        class ElapsedMs final {
          public:
            explicit ElapsedMs(double &into): _into(into), _start(std::chrono::steady_clock::now()) {}
            ~ElapsedMs() {
                _into = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _start).count();
            }

          private:
            double &_into;
            std::chrono::steady_clock::time_point _start;
        };
    } // namespace

    struct ReplicationManager::StateBagFlushPool {
        std::vector<RPC::StateBagSync::Change> changes;
    };

    ReplicationManager::ReplicationManager(): _stateBagFlushPool(std::make_unique<StateBagFlushPool>()) {}
    ReplicationManager::~ReplicationManager() = default;

    void ReplicationManager::SetReplicationWorkerThreads(uint32_t workers) {
        _workerThreads = workers;
        _workersSet    = true;
        _sharedWorkers = nullptr;
        _workers.reset();
    }

    void ReplicationManager::SetReplicationWorkerPool(Utils::ForkJoinPool *pool) {
        _sharedWorkers = pool;
        if (pool != nullptr) {
            _workers.reset();
        }
    }

    Utils::ForkJoinPool *ReplicationManager::Workers(size_t work) {
        if (work < kParallelMin) {
            return nullptr;
        }
        if (_sharedWorkers != nullptr) {
            return _sharedWorkers->Lanes() > 1 ? _sharedWorkers : nullptr;
        }
        if (!_workers) {
            if (!_workersSet) {
                // Every core the tick and network threads leave, up to where the pass stops scaling.
                _workerThreads = Utils::ThreadBudget::For(std::thread::hardware_concurrency()).replicationWorkers;
                _workersSet    = true;
            }
            if (_workerThreads > 0) {
                _workers = std::make_unique<Utils::ForkJoinPool>(_workerThreads);
            }
        }
        return _workers.get();
    }

    void ReplicationManager::SerializeExecutor::ParallelFor(unsigned int count, void (*fn)(void *context, unsigned int index), void *context) {
        Utils::ForkJoinPool *workers = manager->Workers(count);
        if (!workers) {
            for (unsigned int i = 0; i < count; ++i) {
                fn(context, i);
            }
            return;
        }
        workers->Run(count, [fn, context](uint32_t index, uint32_t) {
            fn(context, index);
        });
    }

    namespace {
        thread_local uint32_t t_noPass = 0;
    } // namespace

    ReplicationManager::NoPassScope::NoPassScope(bool active): _active(active) {
        if (_active) {
            ++t_noPass;
        }
    }

    ReplicationManager::NoPassScope::~NoPassScope() {
        if (_active) {
            --t_noPass;
        }
    }

    void ReplicationManager::Update() {
        if (t_noPass != 0) {
            return;
        }
        if (_networkUpdateActive && std::exchange(_updatedThisNetworkUpdate, true)) {
            return;
        }
        const ElapsedMs elapsed(_lastPassMs);
        // Held for the walk over the connections below as well as for the pass.
        const WorldGuard world(*this);
        ApplyPending();
        _preparedDiffs = 0;
        {
            FW_PROFILE_SCOPE_N("Replication::RM3Update");
            MafiaNet::ReplicaManager3::Update();
        }
        _serializeForced = false;
        {
            FW_PROFILE_SCOPE_N("Replication::FlushTransforms");
            // Flush in the same pass that queued the poses, never on the next tick.
            for (unsigned int world = 0; world < worldsList.Size(); ++world) {
                const MafiaNet::WorldId worldId = worldsList[world]->worldId;
                for (unsigned int i = 0; i < GetConnectionCount(worldId); ++i) {
                    static_cast<ReplicationConnection *>(GetConnectionAtIndex(i, worldId))->FlushTransforms(GetRakPeerInterface());
                }
            }
        }
        // Every connection the pass asked has read the re-evaluations; one it did not ask catches up
        // with a full recompute.
        _reevaluationBase += _reevaluations.size();
        _reevaluations.clear();
    }

    void ReplicationManager::SerializeThisPass() {
        // ReplicaManager3 constructs before it checks whether a serialize is due, against the time it
        // read on entry: moving the last pass back one interval makes it due, wrap-around included.
        if (!std::exchange(_serializeForced, true)) {
            lastAutoSerializeOccurance -= autoSerializeInterval;
        }
    }

    MafiaNet::PluginReceiveResult ReplicationManager::OnReceive(MafiaNet::Packet *packet) {
        // The server alone decides what exists: a client's constructions would sit outside every
        // viewer's interest set, streamed to no one and never streamed out.
        if (_isServer && CarriesConstructions(packet)) {
            return MafiaNet::RR_STOP_PROCESSING_AND_DEALLOCATE;
        }
        if (!TransformBatch::IsBatch(packet->data, packet->length)) {
            return MafiaNet::ReplicaManager3::OnReceive(packet);
        }
        MafiaNet::BitStream input(packet->data, packet->length, false);
        MafiaNet::Time timestamp;
        MafiaNet::WorldId worldId;
        TransformBatch::Entries entries;
        if (!TransformBatch::Read(input, timestamp, worldId, entries)) {
            return MafiaNet::RR_STOP_PROCESSING_AND_DEALLOCATE;
        }
        // Delivered as ReplicaManager3 delivers a state message: under the sender's lock, and each
        // entity's from inside Deserialize. Never the world, so a batch does not wait for a pass.
        StateDelivery delivery(*this, packet->guid, worldId);
        if (delivery.GetConnection() == nullptr) {
            return MafiaNet::RR_STOP_PROCESSING_AND_DEALLOCATE;
        }
        // One set of parameters for the whole packet: its pose stream keeps whatever capacity a large
        // pose grew it to.
        MafiaNet::DeserializeParameters parameters {};
        parameters.timeStamp             = timestamp; // RakPeer already shifted ID_TIMESTAMP to local time.
        parameters.bitstreamWrittenTo[0] = true;
        for (const TransformBatch::Entry &entry : entries) {
            parameters.serializationBitstream[0].Reset();
            parameters.serializationBitstream[0].WriteBits(input.GetData() + BITS_TO_BYTES(entry.offset), entry.bits, false);
            // The same owner, epoch and per-entity timestamp gates as an RM3 serialize message. An
            // entity that is not there has its construction still in flight, or a destruction
            // overtook this unreliable batch; the other entries are unaffected.
            delivery.Deserialize(entry.networkId, &parameters);
        }
        return MafiaNet::RR_STOP_PROCESSING_AND_DEALLOCATE;
    }

    void ReplicationManager::ConfigureGrid(float cellSize, float worldMin, float worldMax) {
        _interest.Configure(cellSize, worldMin, worldMax);
    }

    void ReplicationManager::Init(NetworkPeer *owner, bool isServer) {
        _owner    = owner;
        _isServer = isServer;
        _myGUID   = MafiaNet::ToPeerGuid(owner->GetPeer()->GetMyGUID());
        SetNetworkIDManager(owner->GetNetworkIDManager());
        SetDefaultOrderingChannel(ToOrderingChannel(Channel::Construction));
        owner->GetPeer()->AttachPlugin(this);
        _delegation.Init(this, isServer);
        // Every entity broadcasts identically (NetworkEntity::BroadcastsIdentically), so once each is
        // serialized a connection's share of the pass is its own: the server serves them on the
        // replication workers. A client has the one connection.
        if (_isServer) {
            _serializeExecutor.manager = this;
            SetSerializeExecutor(&_serializeExecutor);
        }

        // Client-only: these are server->owner pushes, so the server must never accept them inbound.
        if (!_isServer && !_clientRPCsRegistered) {
            owner->RegisterRawRPC(kForceStateId, [this](MafiaNet::BitStream *bs, MafiaNet::Packet *) {
                MafiaNet::NetworkID networkId;
                bs->ReadCompressed(networkId);
                if (auto *entity = GetEntityByNetworkID(networkId)) {
                    entity->ReadForcedState(bs);
                }
            });
            owner->RegisterRPC<SetOwnerRPC>([this](const SetOwnerRPC &payload, MafiaNet::Packet *) {
                if (auto *entity = GetEntityByNetworkID(payload.networkId)) {
                    entity->ownerGUID  = payload.ownerGUID;
                    entity->stateEpoch = payload.stateEpoch;
                }
            });
            owner->RegisterRPC<RPC::StateBagSync>([this](const RPC::StateBagSync &payload, MafiaNet::Packet *) {
                for (const auto &change : payload.changes) {
                    NetworkEntity *entity = GetEntityByNetworkID(change.networkId);
                    if (entity == nullptr) {
                        // Streamed out between the server's flush and this packet. A normal race;
                        // logging it would bury the failures that matter.
                        continue;
                    }
                    entity->state.Apply(change.key, change.value, change.removed);
                }
            });
            _clientRPCsRegistered = true;
        }
    }

    void ReplicationManager::DeclineSimulation(NetworkEntity *entity, bool declined) {
        if (!entity || !_owner || _isServer) {
            return;
        }
        RPC::DeclineSimulation payload;
        payload.networkId = entity->GetNetworkID();
        payload.declined  = declined;
        _owner->BroadcastRPC(payload);
    }

    uint32_t ReplicationManager::TransformSendIntervalMs(const SerializeRateBands &bands, float distSq) {
        if (distSq <= bands.nearDistance * bands.nearDistance) {
            return bands.nearIntervalMs;
        }
        if (distSq <= bands.midDistance * bands.midDistance) {
            return bands.midIntervalMs;
        }
        return bands.farIntervalMs;
    }

    void ReplicationManager::ForceState(uint64_t networkId) {
        ForceState(_world.Write<NetworkEntity>(networkId));
    }

    void ReplicationManager::SetOwner(uint64_t networkId, MafiaNet::PeerGuid guid) {
        SetOwner(_world.Write<NetworkEntity>(networkId), guid);
    }

    void ReplicationManager::SetVirtualWorld(uint64_t networkId, MafiaNet::VirtualWorldId virtualWorld) {
        SetVirtualWorld(_world.Write<NetworkEntity>(networkId), virtualWorld);
    }

    void ReplicationManager::ForceState(NetworkEntity &held) {
        NetworkEntity *entity = &held;
        // Server-only, like SetOwner: on a client this must be a no-op, not an RPC misaddressed to a
        // peer we aren't connected to (the shared scripting builtins call it on both sides).
        if (!_owner || !_isServer || entity->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
            return;
        }
        // The epoch fences the override: the owner echoes the new value back in its updates, and
        // Deserialize drops owner state still carrying the old one — in-flight packets sent before
        // the owner saw this override can no longer revert it.
        ++entity->stateEpoch;
        MafiaNet::BitStream bs;
        MafiaNet::NetworkID networkId = entity->GetNetworkID();
        // NetworkIDs are small and monotonic, so WriteCompressed strips the leading zero bytes.
        bs.WriteCompressed(networkId);
        entity->WriteForcedState(&bs);
        _owner->SendRawRPC(kForceStateId, bs, MafiaNet::ToGuid(entity->ownerGUID));
    }

    void ReplicationManager::Queue(Pending pending) {
        const std::lock_guard guard(_mutex);
        _pending.push_back(pending);
        _anyPending.store(true, std::memory_order_release);
    }

    void ReplicationManager::ApplyPending() {
        if (!_anyPending.load(std::memory_order_acquire)) {
            return;
        }
        // The index and the connections are this lock's; an entity it finds stays referenced.
        const WorldGuard world(*this);
        std::vector<Pending> pending;
        {
            const std::lock_guard guard(_mutex);
            pending.swap(_pending);
            _anyPending.store(false, std::memory_order_release);
        }
        for (const Pending &entry : pending) {
            if (entry.kind == Pending::Kind::Invalidate) {
                InvalidateInterestOf(entry.guid);
                continue;
            }
            NetworkEntity *entity = GetEntityByNetworkID(entry.id);
            if (entity == nullptr) {
                continue;
            }
            switch (entry.kind) {
            case Pending::Kind::Resync: {
                const Detail::EntityGuard held(*entity, false);
                // An owner among it: see InterestGrid::Reown for what a stale owned index costs the
                // peer that just gained authority.
                SyncInterestOf(entity);
                break;
            }
            case Pending::Kind::Reevaluate: {
                // What changed is what the viewers are about to be asked about.
                {
                    const Detail::EntityGuard held(*entity, false);
                    SyncInterestOf(entity);
                }
                Reevaluate(entity);
                break;
            }
            case Pending::Kind::Unview: {
                const Detail::EntityGuard held(*entity, true);
                const std::lock_guard guard(_mutex);
                const bool viewer = std::any_of(_viewers.begin(), _viewers.end(), [&](const auto &mapping) {
                    return mapping.second == entry.id;
                });
                if (!viewer) {
                    entity->streaming.isViewer = false;
                }
                break;
            }
            case Pending::Kind::Invalidate: break;
            }
        }
    }

    void ReplicationManager::InvalidateInterestOf(MafiaNet::PeerGuid guid) {
        if (guid == MafiaNet::UNASSIGNED_PEER_GUID) {
            return;
        }
        if (auto *connection = static_cast<ReplicationConnection *>(GetConnectionByGUID(MafiaNet::ToGuid(guid)))) {
            connection->InvalidateInterest();
        }
    }

    void ReplicationManager::SetViewerFilter(NetworkEntity &entity, ViewerFilter filter) {
        auto &current = entity.streaming.viewers;
        const ViewerFilter none;
        const ViewerFilter &before = current ? *current : none;
        if (before == filter) {
            return;
        }
        if (before.only.has_value() != filter.only.has_value()) {
            Queue({Pending::Kind::Reevaluate, entity.GetNetworkID()});
        }
        else {
            Queue({Pending::Kind::Resync, entity.GetNetworkID()});
            const ViewerFilter *sides[] {&before, &filter};
            for (const ViewerFilter *side : sides) {
                if (side->only) {
                    for (const MafiaNet::PeerGuid guid : *side->only) {
                        Queue({Pending::Kind::Invalidate, 0, guid});
                    }
                }
                for (const MafiaNet::PeerGuid guid : side->except) {
                    Queue({Pending::Kind::Invalidate, 0, guid});
                }
                Queue({Pending::Kind::Invalidate, 0, side->exceptFollowed});
            }
        }
        if (filter.Empty()) {
            current.reset();
        }
        else {
            current = std::make_unique<ViewerFilter>(std::move(filter));
        }
    }

    void ReplicationManager::SetViewerAdmitted(NetworkEntity &entity, MafiaNet::PeerGuid guid, bool admitted) {
        auto &viewers = entity.streaming.viewers;
        if (!viewers) {
            if (admitted) {
                return;
            }
            viewers = std::make_unique<ViewerFilter>();
        }
        const bool wasAdmitted = viewers->Admits(guid);
        if (admitted) {
            viewers->except.erase(guid);
            if (viewers->only) {
                viewers->only->insert(guid);
            }
        }
        else {
            viewers->except.insert(guid);
            if (viewers->only) {
                viewers->only->erase(guid);
            }
        }
        if (viewers->Admits(guid) != wasAdmitted) {
            Queue({Pending::Kind::Resync, entity.GetNetworkID()});
            Queue({Pending::Kind::Invalidate, 0, guid});
        }
        if (viewers->Empty()) {
            viewers.reset();
        }
    }

    void ReplicationManager::SetStreamTarget(NetworkEntity &entity, MafiaNet::PeerGuid guid) {
        const MafiaNet::PeerGuid previous = entity.streaming.targetGUID;
        if (previous == guid) {
            return;
        }
        entity.streaming.targetGUID = guid;
        if (previous == MafiaNet::UNASSIGNED_PEER_GUID || guid == MafiaNet::UNASSIGNED_PEER_GUID) {
            Queue({Pending::Kind::Reevaluate, entity.GetNetworkID()});
        }
        else {
            Queue({Pending::Kind::Resync, entity.GetNetworkID()});
            Queue({Pending::Kind::Invalidate, 0, previous});
            Queue({Pending::Kind::Invalidate, 0, guid});
        }
    }

    void ReplicationManager::SetOwner(NetworkEntity &held, MafiaNet::PeerGuid guid) {
        NetworkEntity *entity = &held;

        // Captured before the grant: the peer losing authority is holding every owner-scoped value it
        // was ever sent, and a client stores what it is sent regardless of the scope it was sent
        // under, so withholding future updates would leave those values in place. This has to happen
        // here rather than in the flush, which by then would compare the key's audience against the
        // new owner.
        const MafiaNet::PeerGuid previousOwner = entity->ownerGUID;
        if (_owner && _isServer && previousOwner != guid && previousOwner != MafiaNet::UNASSIGNED_PEER_GUID) {
            RPC::StateBagSync revoke;
            for (const std::string &key : entity->state.OwnerRevokeKeys()) {
                RPC::StateBagSync::Change change;
                change.networkId = entity->GetNetworkID();
                change.key       = key;
                change.removed   = true;
                revoke.changes.push_back(change);
            }
            if (!revoke.changes.empty()) {
                _owner->SendRPC(revoke, MafiaNet::ToGuid(previousOwner));
            }
        }

        entity->ownerGUID = guid;
        entity->ResetTransformOrdering();
        // Owned entities bypass range and budget, so the two owners' sets changed and nobody else's
        // did: only they skip their refresh phase. Applied before anything queries relevance again.
        if (previousOwner != guid) {
            Queue({Pending::Kind::Resync, entity->GetNetworkID()});
            Queue({Pending::Kind::Invalidate, 0, previousOwner});
            Queue({Pending::Kind::Invalidate, 0, guid});
        }
        // Serialize to an owner is withheld, so the grant can't ride normal replication: tell the new
        // owner directly. Other peers (and any prior owner) pick it up through serialize.
        if (_owner && _isServer && guid != MafiaNet::UNASSIGNED_PEER_GUID) {
            SetOwnerRPC payload;
            payload.networkId  = entity->GetNetworkID();
            payload.ownerGUID  = guid;
            payload.stateEpoch = entity->stateEpoch;
            _owner->SendRPC(payload, MafiaNet::ToGuid(guid));

            // Owner-scoped keys only ever went to the previous owner, and this owner's construction
            // snapshot predates the grant. Nothing else would resend them.
            const std::vector<std::string> ownerKeys = entity->state.OwnerKeys();
            if (!ownerKeys.empty()) {
                RPC::StateBagSync seed;
                for (const std::string &key : ownerKeys) {
                    const StateValue *value = entity->state.Get(key);
                    if (value == nullptr) {
                        continue;
                    }
                    RPC::StateBagSync::Change change;
                    change.networkId = entity->GetNetworkID();
                    change.key       = key;
                    change.value     = *value;
                    seed.changes.push_back(change);
                }
                if (!seed.changes.empty()) {
                    _owner->SendRPC(seed, MafiaNet::ToGuid(guid));
                }
            }
        }
    }

    StateChangeHandle ReplicationManager::AddStateChangeHandler(const StateChangeFilter &filter, fu2::function<void(const StateChange &) const> callback) {
        if (!callback) {
            return kInvalidStateChangeHandle;
        }

        const std::lock_guard guard(_mutex);
        const StateChangeHandle handle = ++_nextStateChangeHandle;
        _stateChangeHandlers.emplace(handle, StateChangeSubscription {filter, std::make_shared<const fu2::function<void(const StateChange &) const>>(std::move(callback))});
        _anyStateChangeHandlers = true;
        if (filter.key.empty()) {
            _stateChangeAnyKey.push_back(handle);
        }
        else {
            _stateChangeByKey[filter.key].push_back(handle);
        }
        return handle;
    }

    void ReplicationManager::RemoveStateChangeHandler(StateChangeHandle handle) {
        const std::lock_guard guard(_mutex);
        const auto it = _stateChangeHandlers.find(handle);
        if (it == _stateChangeHandlers.end()) {
            return;
        }

        const std::string key = it->second.filter.key;
        _stateChangeHandlers.erase(it);
        _anyStateChangeHandlers = !_stateChangeHandlers.empty();

        const auto drop = [handle](std::vector<StateChangeHandle> &bucket) {
            std::erase(bucket, handle);
        };
        if (key.empty()) {
            drop(_stateChangeAnyKey);
            return;
        }
        const auto bucket = _stateChangeByKey.find(key);
        if (bucket != _stateChangeByKey.end()) {
            drop(bucket->second);
            if (bucket->second.empty()) {
                _stateChangeByKey.erase(bucket);
            }
        }
    }

    void ReplicationManager::NotifyStateChanged(const StateChange &change) {
        {
            const std::lock_guard guard(_mutex);
            if (!_anyStateChangeHandlers) {
                return;
            }
        }
        // A handler asks the world for the entity, which the writer holds: on the server it is told
        // once the writer has let go, by id.
        if (_isServer && World::HeldByThisThread() != 0) {
            World::WhenNoneHeld([this, later = change] {
                DispatchStateChange(later);
            });
            return;
        }
        DispatchStateChange(change);
    }

    void ReplicationManager::DispatchStateChange(const StateChange &change) {
        std::vector<StateChangeHandle> matched;
        {
            const std::lock_guard guard(_mutex);
            const auto collect = [&](const std::vector<StateChangeHandle> &bucket) {
                for (const StateChangeHandle handle : bucket) {
                    const auto it = _stateChangeHandlers.find(handle);
                    if (it == _stateChangeHandlers.end()) {
                        continue;
                    }
                    const uint64_t wanted = it->second.filter.networkId;
                    if (wanted == 0 || wanted == change.networkId) {
                        matched.push_back(handle);
                    }
                }
            };

            collect(_stateChangeAnyKey);
            const auto keyed = _stateChangeByKey.find(change.key);
            if (keyed != _stateChangeByKey.end()) {
                collect(keyed->second);
            }
        }

        // Each handle is looked up again at call time: a handler is free to unsubscribe itself or
        // another during dispatch, and one added during dispatch is not called for this change.
        for (const StateChangeHandle handle : matched) {
            std::shared_ptr<const fu2::function<void(const StateChange &) const>> callback;
            {
                const std::lock_guard guard(_mutex);
                const auto it = _stateChangeHandlers.find(handle);
                if (it != _stateChangeHandlers.end()) {
                    callback = it->second.callback;
                }
            }
            if (callback && *callback) {
                (*callback)(change);
            }
        }
    }

    void ReplicationManager::MarkStateBagDirty(uint64_t networkId) {
        const std::lock_guard guard(_mutex);
        _dirtyStateBags.insert(networkId);
    }

    std::vector<MafiaNet::PeerGuid> ReplicationManager::StreamingPeers(uint64_t networkId) {
        std::vector<MafiaNet::PeerGuid> peers;
        if (!_isServer || _owner == nullptr) {
            return peers;
        }
        World::RequireNoneHeld("an entity's audience was asked for");
        const WorldGuard world(*this);
        NetworkEntity *entity = GetEntityByNetworkID(networkId);
        if (entity == nullptr) {
            return peers;
        }
        const unsigned connectionCount = GetConnectionCount();
        for (unsigned i = 0; i < connectionCount; ++i) {
            MafiaNet::Connection_RM3 *connection = GetConnectionAtIndex(i);
            if (connection != nullptr && connection->HasReplicaConstructed(entity)) {
                peers.push_back(MafiaNet::ToPeerGuid(connection->GetRakNetGUID()));
            }
        }
        return peers;
    }

    void ReplicationManager::FlushStateBags() {
        std::unordered_set<uint64_t> dirty;
        {
            const std::lock_guard guard(_mutex);
            dirty.swap(_dirtyStateBags);
        }
        if (dirty.empty()) {
            return;
        }
        FW_PROFILE_SCOPE_N("Replication::FlushStateBags");
        FW_PROFILE_VALUE(dirty.size());
        // For the walk over the connections; an entity found under it stays referenced.
        const WorldGuard world(*this);

        // A client sends nothing outbound, and with no connections there is nowhere to send. Drop
        // the marks rather than let them accumulate for a flush that never comes.
        const unsigned connectionCount = (_isServer && _owner != nullptr) ? GetConnectionCount() : 0;
        if (connectionCount == 0) {
            for (const uint64_t networkId : dirty) {
                if (auto *entity = GetEntityByNetworkID(networkId)) {
                    const Detail::EntityGuard held(*entity, true);
                    entity->state.ClearDirty();
                }
            }
            return;
        }

        // Each change is built once into a shared pool; a connection is sent the indices of the ones
        // it should get, so a change for many viewers is not copied per viewer. Placement asks the
        // connection whether it has the entity constructed — the question interest, virtual worlds
        // and budgets have already answered, so none of it is repeated here.
        _stateBagFlushPool->changes.clear();
        _stateBagFlushIndices.resize(connectionCount);
        for (auto &indices : _stateBagFlushIndices) {
            indices.clear();
        }
        for (const uint64_t networkId : dirty) {
            NetworkEntity *entity = GetEntityByNetworkID(networkId);
            if (entity == nullptr) {
                continue; // Destroyed since it dirtied.
            }
            const Detail::EntityGuard held(*entity, true);

            for (const auto &[key, dirty] : entity->state.Dirty()) {
                const StateValue *current = entity->state.Get(key);

                const auto updateIndex            = static_cast<uint32_t>(_stateBagFlushPool->changes.size());
                RPC::StateBagSync::Change &update = _stateBagFlushPool->changes.emplace_back();
                update.networkId                  = entity->GetNetworkID();
                update.key                        = key;
                update.removed                    = current == nullptr;
                if (current != nullptr) {
                    update.value = *current;
                }

                // Sent to a connection that held the key and is no longer meant to. Nothing else takes
                // a value back off a peer: a client stores what it is sent and filtering later updates
                // would leave the old one sitting there. Built the first time a connection needs it,
                // since most keys never narrow.
                std::optional<uint32_t> revokeIndex;
                const auto revoke = [&]() {
                    if (!revokeIndex.has_value()) {
                        revokeIndex                       = static_cast<uint32_t>(_stateBagFlushPool->changes.size());
                        RPC::StateBagSync::Change &change = _stateBagFlushPool->changes.emplace_back();
                        change.networkId                  = entity->GetNetworkID();
                        change.key                        = key;
                        change.removed                    = true;
                    }
                    return *revokeIndex;
                };

                for (unsigned i = 0; i < connectionCount; ++i) {
                    MafiaNet::Connection_RM3 *connection = GetConnectionAtIndex(i);
                    if (connection == nullptr || !connection->HasReplicaConstructed(entity)) {
                        continue;
                    }

                    const MafiaNet::PeerGuid peer = MafiaNet::ToPeerGuid(connection->GetRakNetGUID());
                    const auto inAudience         = [&](StateScope scope) {
                        switch (scope) {
                        case StateScope::Broadcast: return true;
                        case StateScope::Owner: return peer == entity->ownerGUID;
                        case StateScope::Server: return false;
                        }
                        return false;
                    };

                    if (inAudience(dirty.scope)) {
                        _stateBagFlushIndices[i].push_back(updateIndex);
                    }
                    else if (dirty.previous.has_value() && inAudience(*dirty.previous)) {
                        _stateBagFlushIndices[i].push_back(revoke());
                    }
                }
            }
            entity->state.ClearDirty();
        }

        for (unsigned i = 0; i < connectionCount; ++i) {
            const std::vector<uint32_t> &indices = _stateBagFlushIndices[i];
            if (indices.empty()) {
                continue;
            }
            MafiaNet::Connection_RM3 *connection = GetConnectionAtIndex(i);
            if (connection == nullptr) {
                continue;
            }
            // Chunked to what the receiver accepts, so the reader never trusts a count off the wire.
            for (size_t offset = 0; offset < indices.size(); offset += RPC::StateBagSync::kMaxChanges) {
                const size_t end = std::min(offset + RPC::StateBagSync::kMaxChanges, indices.size());
                StateBagSyncView view;
                view.pool    = &_stateBagFlushPool->changes;
                view.indices = indices.data() + offset;
                view.count   = static_cast<uint16_t>(end - offset);
                _owner->SendRPC(view, connection->GetRakNetGUID());
            }
        }
    }

    NetworkEntity *ReplicationManager::CreateEntity(uint32_t typeId, Detail::WorldHold &hold) {
        NetworkEntity *entity = EntityRegistry::Get().Create(typeId);
        if (!entity) {
            return nullptr;
        }
        // Assign a small, sequential id before Reference() so the NetworkIDManager tracks the entity
        // under it. Ids must stay within JavaScript's 2^53 exact-integer range so scripts can hold
        // them as plain numbers. Clients adopt this id via the construction snapshot.
        entity->SetNetworkID(_nextNetworkId.fetch_add(1) + 1);
        // Its lock first, held for the creator: once referenced, the tick finds the entity through
        // ReplicaManager3 and must wait until the creator has let go of it.
        std::shared_ptr<Detail::WorldEntitySlot> slot = _world.Prepare(entity);
        {
            const WorldGuard world(*this);
            Reference(entity);
            // Into the index now, so it has its slot; every viewer decides on it at its next query,
            // from what the index takes of it then: by that time the caller has placed it.
            _interest.Sync(entity);
            Queue({Pending::Kind::Reevaluate, entity->GetNetworkID()});
        }
        // Before the id resolves and before the lock counts as one this thread holds: the callback
        // may ask the world for other entities, and finds nothing under this id.
        if (_onEntityCreated) {
            _onEntityCreated(entity->GetNetworkID());
        }
        _world.Publish(std::move(slot), hold);
        return entity;
    }

    bool ReplicationManager::DestroyByOwner(NetworkEntity *entity, MafiaNet::Connection_RM3 *source) {
        const uint64_t id = entity->GetNetworkID();
        std::shared_ptr<Detail::WorldEntitySlot> unheld;
        {
            // Alone with the entity from the owner check to the mod's PreDestruction: a handler or a
            // script that held it has let go, and none takes it in between. (ReplicaManager3's world,
            // the sender's connection, then the entity: the order everything else takes them in.)
            const Detail::EntityGuard guard(*entity, true);
            if (MafiaNet::ToPeerGuid(source->GetRakNetGUID()) != entity->ownerGUID || !entity->CanOwnerDestroy()) {
                return false;
            }
            if (_world.MarkDoomed(id, unheld) != World::Doom::Marked) {
                return false;
            }
            if (unheld) {
                entity->PreDestruction(source);
            }
            else {
                // A handle holds it still, or waits for it: its holder must not see the entity
                // half taken apart. The teardown that the last release brings runs it, once.
                entity->_preDestructionOwed = true;
            }
        }
        if (unheld) {
            _world.TeardownUnheld(std::move(unheld));
        }
        return !_world.IsDoomed(id);
    }

    void ReplicationManager::TeardownEntity(NetworkEntity *entity) {
        const uint64_t id = entity->GetNetworkID();
        if (std::exchange(entity->_preDestructionOwed, false)) {
            // The connection that asked may be gone by now, so none is named.
            entity->PreDestruction(nullptr);
        }
        {
            const WorldGuard world(*this);
            {
                // Erase by value, not by ownerGUID: the owner key may have been reassigned since
                // SetViewer, and on a respawn flow (SetViewer(newAvatar) then destroying the old one)
                // an erase by key would take out the replacement's mapping and silence that
                // connection's streaming.
                const std::lock_guard guard(_mutex);
                std::erase_if(_viewers, [id](const auto &mapping) {
                    return mapping.second == id;
                });
            }
            // Out of the index and every viewer's set, so this delete can't dangle, and not back
            // into it: the tick may sync the index between here and the dereference below.
            entity->_leaving = true;
            ForgetEntity(entity);
        }
        // And the delegation bookkeeping, which is keyed by NetworkID: ids are monotonic, but an
        // entry left behind would still be found by a later entity that reused the id after a
        // reconnect, and would carry a stale pin into it.
        _delegation.OnEntityDestroyed(id);
        if (_onEntityDestroyed) {
            _onEntityDestroyed(id);
        }
        // BroadcastDestruction must precede Dereference, and both the deletion.
        entity->BroadcastDestruction();
        Dereference(entity);
        delete entity;
    }

    NetworkEntity *ReplicationManager::GetEntityByNetworkID(MafiaNet::NetworkID networkId) const {
        auto *idm = GetNetworkIDManager();
        if (!idm || _world.IsDoomed(networkId)) {
            return nullptr;
        }
        return idm->GET_OBJECT_FROM_ID<NetworkEntity *>(networkId);
    }

    void ReplicationManager::ForEachEntity(const fu2::function<void(NetworkEntity *) const> &fn) const {
        ForEachReferenced([this, &fn](NetworkEntity *entity) {
            if (!DestroyPending(entity)) {
                fn(entity);
            }
        });
    }

    void ReplicationManager::ForEachReferenced(const fu2::function<void(NetworkEntity *) const> &fn) const {
        const unsigned count = GetReplicaCount();
        for (unsigned i = 0; i < count; ++i) {
            auto *entity = static_cast<NetworkEntity *>(GetReplicaAtIndex(i));
            if (entity) {
                fn(entity);
            }
        }
    }

    void ReplicationManager::SetViewer(MafiaNet::PeerGuid guid, uint64_t networkId) {
        if (networkId == 0) {
            ClearViewer(guid);
            return;
        }
        // A replaced avatar stops being a viewer, or the flag would outlive the mapping and confuse
        // later lifecycle decisions keyed on it.
        const uint64_t previousId = GetViewerId(guid);
        auto [previous, entity]   = _world.Write<NetworkEntity, NetworkEntity>(previousId, networkId);
        if (!entity) {
            // Asked for twice, the one handle is the first.
            if (previousId == networkId && previous) {
                SetViewer(guid, *previous);
            }
            return;
        }
        if (previous) {
            previous->streaming.isViewer = false;
        }
        SetViewer(guid, *entity);
    }

    void ReplicationManager::SetViewer(MafiaNet::PeerGuid guid, NetworkEntity &entity) {
        entity.streaming.isViewer = true;
        const uint64_t id         = entity.GetNetworkID();
        uint64_t previous         = 0;
        {
            const std::lock_guard guard(_mutex);
            uint64_t &mapped = _viewers[guid];
            previous         = std::exchange(mapped, id);
        }
        if (previous != 0 && previous != id) {
            Queue({Pending::Kind::Unview, previous});
        }
    }

    uint64_t ReplicationManager::GetViewerId(MafiaNet::PeerGuid guid) const {
        uint64_t id = 0;
        {
            const std::lock_guard guard(_mutex);
            const auto it = _viewers.find(guid);
            if (it == _viewers.end()) {
                return 0;
            }
            id = it->second;
        }
        return DestroyPending(id) ? 0 : id;
    }

    std::vector<std::pair<MafiaNet::PeerGuid, uint64_t>> ReplicationManager::GetViewerIds() const {
        std::vector<std::pair<MafiaNet::PeerGuid, uint64_t>> viewers;
        {
            const std::lock_guard guard(_mutex);
            viewers.assign(_viewers.begin(), _viewers.end());
        }
        std::erase_if(viewers, [this](const auto &viewer) {
            return DestroyPending(viewer.second);
        });
        return viewers;
    }

    NetworkEntity *ReplicationManager::GetViewer(MafiaNet::PeerGuid guid) const {
        const uint64_t id = GetViewerId(guid);
        return id != 0 ? GetEntityByNetworkID(id) : nullptr;
    }

    void ReplicationManager::ClearViewer(MafiaNet::PeerGuid guid) {
        uint64_t id = 0;
        {
            const std::lock_guard guard(_mutex);
            const auto it = _viewers.find(guid);
            if (it == _viewers.end()) {
                return;
            }
            id = it->second;
            _viewers.erase(it);
        }
        if (World::HeldByThisThread() != 0) {
            Queue({Pending::Kind::Unview, id});
        }
        else if (const Ref<NetworkEntity> entity = _world.Write<NetworkEntity>(id)) {
            entity->streaming.isViewer = false;
        }
    }

    void ReplicationManager::Reevaluate(NetworkEntity *entity) {
        const uint32_t slot = _interest.SlotOf(entity);
        if (slot != InterestGrid::kNoSlot) {
            _reevaluations.emplace_back(slot, _interest.GenerationOf(slot));
        }
    }

    void ReplicationManager::ForgetEntity(NetworkEntity *entity) {
        const WorldGuard world(*this);
        _world.Forget(entity);
        const uint32_t slot = _interest.SlotOf(entity);
        if (slot == InterestGrid::kNoSlot) {
            return;
        }
        _interest.Remove(entity);
        ForgetSlot(slot);
    }

    void ReplicationManager::ForgetSlot(uint32_t slot) {
        const unsigned count = GetConnectionCount();
        for (unsigned i = 0; i < count; ++i) {
            static_cast<ReplicationConnection *>(GetConnectionAtIndex(i))->ForgetSlot(slot);
        }
    }

    void ReplicationManager::RebuildInterest() {
        if (!_isServer) {
            return;
        }
        const ElapsedMs elapsed(_lastInterestMs);
        FW_PROFILE_SCOPE_N("Replication::RebuildInterest");
        // The index is this lock's, and so are the connections the workers below recompute; they do
        // not take it, and no entity lock is held here while they run.
        const WorldGuard world(*this);
        ApplyPending();
        {
            FW_PROFILE_SCOPE_N("Replication::SyncInterest");
            _interest.BeginSync();
            ForEachReferenced([this](NetworkEntity *entity) {
                if (entity->_leaving) {
                    return;
                }
                const Detail::EntityGuard held(*entity, false);
                _interest.Sync(entity);
            });
            for (const uint32_t slot : _interest.EndSync()) {
                ForgetSlot(slot);
            }
            for (const uint32_t slot : _interest.DependencyChanges()) {
                _reevaluations.emplace_back(slot, _interest.GenerationOf(slot));
            }
        }

        _dueConnections.clear();
        const unsigned count = GetConnectionCount();
        if (count == 0) {
            return;
        }

        // This tick's share of the viewers, measured in ticks rather than wall time: a tick that ran
        // long earns no more turns than any other.
        uint32_t turns = count;
        if (_interestRebuildInterval > 0) {
            const double tickMs = static_cast<double>(std::max<MafiaNet::Time>(autoSerializeInterval, 1));
            _refreshCredit      = std::min(_refreshCredit + count * tickMs / _interestRebuildInterval, static_cast<double>(count));
            turns               = static_cast<uint32_t>(_refreshCredit);
            _refreshCredit -= turns;
        }
        for (unsigned i = 0; i < count; ++i) {
            const unsigned index = (_refreshCursor + i) % count;
            auto *connection     = static_cast<ReplicationConnection *>(GetConnectionAtIndex(index));
            if (i < turns || connection->InterestUrgent()) {
                _dueConnections.push_back(connection);
            }
        }
        _refreshCursor = (_refreshCursor + turns) % count;
        FW_PROFILE_VALUE(_dueConnections.size());
        ComputeDueInterest();
    }

    void ReplicationManager::ComputeDueInterest() {
        FW_PROFILE_SCOPE_N("Replication::CollectInterest");
        Utils::ForkJoinPool *workers = Workers(_dueConnections.size());
        const uint32_t lanes         = workers ? workers->Lanes() : 1;
        if (_scratch.size() < lanes) {
            _scratch.resize(lanes);
        }
        const auto compute = [this](uint32_t index, uint32_t lane) {
            _dueConnections[index]->ComputeInterest(_scratch[lane]);
        };
        if (workers) {
            workers->Run(static_cast<uint32_t>(_dueConnections.size()), compute);
        }
        else {
            for (uint32_t i = 0; i < _dueConnections.size(); ++i) {
                compute(i, 0);
            }
        }
    }

    void ReplicationManager::OnClosedConnection(const MafiaNet::SystemAddress &systemAddress, MafiaNet::RakNetGUID rakNetGUID, MafiaNet::PI2_LostConnectionReason lostConnectionReason) {
        // The player's avatar is server-created, so the base PopConnection (which only tears down
        // replicas a dropped peer itself created) leaves it behind. Notify the game while the avatar
        // is still resolvable, then destroy it, which broadcasts the destruction to the
        // remaining clients and clears the viewer mapping. Clients keep the base behaviour: their
        // replicas all originate from the server, so PopConnection cleans them up on its own.
        if (_isServer) {
            const auto guid = MafiaNet::ToPeerGuid(rakNetGUID);
            // This plugin callback fires for every closed connection, including peers dropped before
            // they completed identity (build mismatch, quit during the asset phase). Those never
            // produced a player-connect notification, so gate the disconnect notification on the
            // replication connection that PushReplicationConnection creates alongside it — keeping
            // the connect/disconnect callbacks paired for the game.
            if (_onClientDisconnect && HasConnection(rakNetGUID)) {
                _onClientDisconnect(guid);
            }
            // Before the blanket hand-back below, so entities this peer was *simulating* go through
            // delegation and raise its change notification, rather than silently losing an owner.
            _delegation.OnClientDisconnected(guid);
            const uint64_t viewer = GetViewerId(guid);
            // Return any other entity the dropped peer owned (e.g. a vehicle it was driving) to the
            // server, or its authority gate would freeze it against an owner that no longer exists.
            _world.ForEach<NetworkEntity>([&](Ref<NetworkEntity> &entity) {
                if (entity->GetNetworkID() != viewer && entity->ownerGUID == guid) {
                    SetOwner(*entity, MafiaNet::UNASSIGNED_PEER_GUID);
                }
                entity->streaming.Forget(guid);
            });
            if (viewer != 0) {
                _world.Destroy(viewer);
            }
            // Now that the departed peer can no longer be elected, re-run the election so whatever
            // it was simulating is picked up by whoever else is near it this instant rather than at
            // the next scheduled pass.
            _delegation.UpdateNow();
        }
        ReplicaManager3::OnClosedConnection(systemAddress, rakNetGUID, lostConnectionReason);
    }

    bool ReplicationManager::HasConnection(MafiaNet::RakNetGUID guid) const {
        // Only whether there is one: ReplicaManager3 looks it up under its list lock, and the
        // pointer, which another thread may pop, is not kept.
        return GetConnectionByGUID(guid) != nullptr;
    }

    void ReplicationManager::StartReplication(const MafiaNet::SystemAddress &address, MafiaNet::RakNetGUID guid) {
        World::RequireNoneHeld("replication to a connection was started");
        const WorldGuard world(*this);
        if (GetConnectionByGUID(guid) != nullptr) {
            return; // already started (a client could send ClientIdentity twice)
        }
        if (MafiaNet::Connection_RM3 *connection = AllocConnection(address, guid)) {
            PushConnection(connection);
        }
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
