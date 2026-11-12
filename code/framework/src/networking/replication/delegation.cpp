/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "delegation.h"

#include "replication_manager.h"
#include "world.h"

#include <logging/logger.h>
#include <utils/profiler.h>

#include <mafianet/virtual_world.h>

#include <algorithm>
#include <limits>

namespace Framework::Networking::Replication {
    namespace {
        // Above zero while this thread is inside a pass, a disconnect's walk, or the delivery that
        // follows either: a pass asked for meanwhile is left to the next one.
        thread_local int t_walking = 0;
        // Counts this thread into a pass or walk for a scope, whichever way the scope is left.
        struct Walking final {
            Walking() {
                ++t_walking;
            }
            ~Walking() {
                --t_walking;
            }
            Walking(const Walking &)            = delete;
            Walking &operator=(const Walking &) = delete;
        };
    } // namespace

    // One handover, for the handlers that were subscribed when it was made.
    struct DelegationManager::Notice {
        DelegationManager *manager;
        std::vector<std::pair<SimulatorChangedHandle, std::shared_ptr<const SimulatorChanged>>> handlers;
        uint64_t id;
        MafiaNet::PeerGuid previous;
        MafiaNet::PeerGuid current;
        DelegationChange change;
    };

    std::vector<DelegationManager::Notice> &DelegationManager::Notices() {
        thread_local std::vector<Notice> notices;
        return notices;
    }

    void DelegationManager::DeliverNotices() {
        std::vector<Notice> &pending = Notices();
        while (!pending.empty()) {
            std::vector<Notice> notices;
            notices.swap(pending);
            for (const Notice &notice : notices) {
                for (const auto &[handle, callback] : notice.handlers) {
                    {
                        const std::lock_guard guard(notice.manager->_mutex);
                        if (!notice.manager->_simulatorChanged.contains(handle)) {
                            continue;
                        }
                    }
                    (*callback)(notice.id, notice.previous, notice.current, notice.change);
                }
            }
        }
    }

    void DelegationManager::Init(ReplicationManager *manager, bool isServer) {
        _manager  = manager;
        _isServer = isServer;
    }

    void DelegationManager::SetClientCapacity(MafiaNet::PeerGuid guid, uint32_t maxWeight) {
        if (guid == MafiaNet::UNASSIGNED_PEER_GUID) {
            return;
        }
        const std::lock_guard guard(_mutex);
        _capacityOverrides[guid] = maxWeight;
    }

    void DelegationManager::ClearClientCapacity(MafiaNet::PeerGuid guid) {
        const std::lock_guard guard(_mutex);
        _capacityOverrides.erase(guid);
    }

    void DelegationManager::Suspend(MafiaNet::PeerGuid guid) {
        if (guid == MafiaNet::UNASSIGNED_PEER_GUID) {
            return;
        }
        const std::lock_guard guard(_mutex);
        _suspended.insert(guid);
    }

    void DelegationManager::Resume(MafiaNet::PeerGuid guid) {
        const std::lock_guard guard(_mutex);
        _suspended.erase(guid);
    }

    bool DelegationManager::IsSuspended(MafiaNet::PeerGuid guid) const {
        const std::lock_guard guard(_mutex);
        return _suspended.contains(guid);
    }

    void DelegationManager::Pin(uint64_t networkId, MafiaNet::PeerGuid guid) {
        if (networkId == 0 || !_isServer) {
            return;
        }
        {
            const std::lock_guard guard(_mutex);
            Entry &entry   = _entries[networkId];
            entry.pinned   = true;
            entry.pinnedTo = guid;
        }
        // Immediately, rather than at the next pass: a pin is an instruction, and a script that
        // pins an entity and then talks to it expects the simulator to already be the one it named.
        UpdateNow();
    }

    void DelegationManager::Unpin(uint64_t networkId) {
        const std::lock_guard guard(_mutex);
        const auto it = _entries.find(networkId);
        if (it == _entries.end()) {
            return;
        }
        it->second.pinned   = false;
        it->second.pinnedTo = MafiaNet::UNASSIGNED_PEER_GUID;
    }

    bool DelegationManager::IsPinned(uint64_t networkId) const {
        const std::lock_guard guard(_mutex);
        const auto it = _entries.find(networkId);
        return it != _entries.end() && it->second.pinned;
    }

    MafiaNet::PeerGuid DelegationManager::PinnedTo(uint64_t networkId) const {
        const std::lock_guard guard(_mutex);
        const auto it = _entries.find(networkId);
        return it != _entries.end() && it->second.pinned ? it->second.pinnedTo : MafiaNet::UNASSIGNED_PEER_GUID;
    }

    void DelegationManager::SetDeclined(uint64_t networkId, MafiaNet::PeerGuid guid, bool declined) {
        if (!_isServer || !_manager || guid == MafiaNet::UNASSIGNED_PEER_GUID) {
            return;
        }
        {
            const ConstRef<NetworkEntity> entity = _manager->GetWorld().Read<NetworkEntity>(networkId);
            if (!entity || entity->GetDelegationPolicy() == nullptr) {
                return;
            }
        }
        const std::lock_guard guard(_mutex);
        Entry &entry     = _entries[networkId];
        auto &list       = entry.declined;
        const bool known = std::ranges::contains(list, guid);
        if (known == declined) {
            return;
        }
        if (declined) {
            list.push_back(guid);
        }
        else {
            std::erase(list, guid);
        }
        // Coalesced rather than run here, since the RPC behind this is not rate limited. A pinned
        // entity's simulator does not depend on the answer, so it needs no pass at all.
        _passPending = _passPending || !entry.pinned;
    }

    bool DelegationManager::IsDeclined(uint64_t networkId, MafiaNet::PeerGuid guid) const {
        const std::lock_guard guard(_mutex);
        const auto it = _entries.find(networkId);
        return it != _entries.end() && std::ranges::contains(it->second.declined, guid);
    }

    void DelegationManager::OnEntityDestroyed(uint64_t networkId) {
        const std::lock_guard guard(_mutex);
        _entries.erase(networkId);
    }

    void DelegationManager::OnClientDisconnected(MafiaNet::PeerGuid guid) {
        if (!_isServer || !_manager) {
            return;
        }
        {
            const std::lock_guard guard(_mutex);
            _suspended.erase(guid);
            for (auto &[id, entry] : _entries) {
                std::erase(entry.declined, guid);
            }
        }
        // The dropped peer is still the owner of everything it was simulating, and an owner that is
        // gone can never report again. Hand those entities back to the server here, so they are
        // dormant rather than owned-by-nobody until the next pass, and drop any pin naming that
        // client -- a pin is to a player, and that player has left.
        const Walking walking;
        _manager->GetWorld().ForEach<NetworkEntity>([&](Ref<NetworkEntity> &entity) {
            if (entity->GetDelegationPolicy() == nullptr) {
                return;
            }
            {
                const std::lock_guard guard(_mutex);
                const auto it = _entries.find(entity->GetNetworkID());
                if (it != _entries.end() && it->second.pinned && it->second.pinnedTo == guid) {
                    it->second.pinned   = false;
                    it->second.pinnedTo = MafiaNet::UNASSIGNED_PEER_GUID;
                }
            }
            if (entity->ownerGUID == guid) {
                Assign(*entity, MafiaNet::UNASSIGNED_PEER_GUID, DelegationChange::Dormant);
            }
        });
        // Deliberately no re-election here: this runs while the dropped peer's viewer may still be
        // in the viewer map, and a pass now would hand its entities straight back to it. The caller
        // runs one once the viewer is gone. A handler told of the handovers above cannot start one
        // either: what it asks for is the next pass.
        if (t_walking == 1) {
            DeliverNotices();
        }
    }

    void DelegationManager::Update() {
        if (!_isServer || !_manager) {
            return;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();
        {
            const std::lock_guard guard(_mutex);
            const MafiaNet::Time since = now - _lastPass;
            const bool due             = _intervalMs == 0 || _lastPass == 0 || since >= static_cast<MafiaNet::Time>(_intervalMs);
            const bool hurried         = _passPending && since >= static_cast<MafiaNet::Time>(kDeclinedPassGapMs);
            if (!due && !hurried) {
                return;
            }
        }
        UpdateNow();
    }

    DelegationCandidate *DelegationManager::FindCandidate(MafiaNet::PeerGuid guid) {
        if (guid == MafiaNet::UNASSIGNED_PEER_GUID) {
            return nullptr;
        }
        for (DelegationCandidate &candidate : _candidates) {
            if (candidate.guid == guid) {
                return &candidate;
            }
        }
        return nullptr;
    }

    namespace {
        // Ground-plane distance² between two points: X and then whichever of Y or Z the game calls
        // horizontal. Measuring in three dimensions would let a player on a bridge lose an entity
        // standing under it.
        float GroundDistanceSq(const glm::vec3 &a, const glm::vec3 &b, bool groundXY) {
            const glm::vec3 delta = a - b;
            const float u         = delta.x;
            const float v         = groundXY ? delta.y : delta.z;
            return u * u + v * v;
        }
    } // namespace

    MafiaNet::PeerGuid DelegationManager::Elect(const DelegationPolicy &rawPolicy, const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld, MafiaNet::PeerGuid current, const std::vector<DelegationCandidate> &candidates, bool groundXY,
        std::span<const MafiaNet::PeerGuid> declined) {
        const DelegationPolicy policy = rawPolicy.Normalized();
        MafiaNet::PeerGuid desired    = MafiaNet::UNASSIGNED_PEER_GUID;
        float bestRank                = std::numeric_limits<float>::max();
        const auto isDeclined         = [&](MafiaNet::PeerGuid guid) {
            return std::ranges::contains(declined, guid);
        };

        const DelegationCandidate *incumbent = nullptr;
        for (const DelegationCandidate &candidate : candidates) {
            if (candidate.guid != MafiaNet::UNASSIGNED_PEER_GUID && candidate.guid == current && !isDeclined(current)) {
                incumbent = &candidate;
                break;
            }
        }

        // The incumbent is measured against the release range and ranked with the sticky discount,
        // which together are the hysteresis. It is never dropped for being over budget.
        if (incumbent != nullptr) {
            // The same test replication itself streams by, rather than plain equality: the global
            // world sees every other one, and an entity there must not be refused a simulator
            // standing in a private world it can already see.
            const bool worldOk = !policy.requireSameVirtualWorld || MafiaNet::VirtualWorldsCanSee(virtualWorld, incumbent->virtualWorld);
            const float distSq = GroundDistanceSq(position, incumbent->position, groundXY);
            if (worldOk && distSq <= policy.releaseRange * policy.releaseRange) {
                if (!policy.proximitySteal) {
                    return current;
                }
                desired  = current;
                bestRank = distSq * kStickyDiscount;
            }
        }

        for (const DelegationCandidate &candidate : candidates) {
            if (candidate.guid == MafiaNet::UNASSIGNED_PEER_GUID || candidate.guid == current || isDeclined(candidate.guid)) {
                continue;
            }
            if (policy.requireSameVirtualWorld && !MafiaNet::VirtualWorldsCanSee(virtualWorld, candidate.virtualWorld)) {
                continue;
            }
            if (candidate.capacity != 0 && candidate.load + policy.weight > candidate.capacity) {
                continue;
            }
            const float distSq = GroundDistanceSq(position, candidate.position, groundXY);
            if (distSq > policy.acquireRange * policy.acquireRange || distSq >= bestRank) {
                continue;
            }
            bestRank = distSq;
            desired  = candidate.guid;
        }

        return desired;
    }

    uint32_t DelegationManager::CapacityOf(MafiaNet::PeerGuid guid) const {
        const std::lock_guard guard(_mutex);
        const auto override = _capacityOverrides.find(guid);
        return override != _capacityOverrides.end() ? override->second : _defaultCapacity;
    }

    void DelegationManager::CollectCandidates() {
        _candidates.clear();
        World &world = _manager->GetWorld();
        for (const auto &[guid, id] : _manager->GetViewerIds()) {
            if (guid == MafiaNet::UNASSIGNED_PEER_GUID || IsSuspended(guid)) {
                continue;
            }
            const ConstRef<NetworkEntity> viewer = world.Read<NetworkEntity>(id);
            if (!viewer) {
                continue;
            }
            DelegationCandidate candidate;
            candidate.guid         = guid;
            candidate.position     = viewer->position;
            candidate.virtualWorld = viewer->GetVirtualWorld();
            candidate.capacity     = CapacityOf(guid);
            _candidates.push_back(candidate);
        }
    }

    void DelegationManager::Assign(NetworkEntity &entity, MafiaNet::PeerGuid next, DelegationChange change) {
        const MafiaNet::PeerGuid previous = entity.ownerGUID;
        if (previous == next) {
            return;
        }

        _manager->SetOwner(entity, next);
        // The field channel is withheld from an entity's owner, so the peer that has just been told
        // to simulate this entity is the one peer normal replication will never tell what the server
        // wants of it. The forced-state channel is that gap's only bridge, and a handover is exactly
        // when it has to be crossed: without this the new simulator runs the entity from whatever it
        // happened to hold as an observer.
        if (next != MafiaNet::UNASSIGNED_PEER_GUID) {
            _manager->ForceState(entity);
        }

        entity.OnSimulatorChanged(previous, next);
        // A copy of the handlers, so one that subscribes or unsubscribes from inside the dispatch
        // does not rehash the map being walked, and none is called under the mutex.
        std::vector<std::pair<SimulatorChangedHandle, std::shared_ptr<const SimulatorChanged>>> dispatch;
        {
            const std::lock_guard guard(_mutex);
            dispatch.assign(_simulatorChanged.begin(), _simulatorChanged.end());
            ++_stats.handovers;
        }
        if (dispatch.empty()) {
            return;
        }
        // Kept for the end of the pass or walk this is part of: a handler runs with the entity let
        // go, ReplicaManager3's world released and nothing being walked.
        Notices().push_back({this, std::move(dispatch), static_cast<uint64_t>(entity.GetNetworkID()), previous, next, change});
    }

    DelegationManager::SimulatorChangedHandle DelegationManager::AddSimulatorChangedHandler(SimulatorChanged callback) {
        if (!callback) {
            return kInvalidSimulatorHandle;
        }
        const std::lock_guard guard(_mutex);
        const SimulatorChangedHandle handle = ++_nextSimulatorHandle;
        _simulatorChanged[handle]           = std::make_shared<const SimulatorChanged>(std::move(callback));
        return handle;
    }

    void DelegationManager::RemoveSimulatorChangedHandler(SimulatorChangedHandle handle) {
        const std::lock_guard guard(_mutex);
        _simulatorChanged.erase(handle);
    }

    void DelegationManager::UpdateNow() {
        if (!_isServer || !_manager) {
            return;
        }
        // Asked for from inside a pass, a disconnect's walk or a handler: the next pass, not one
        // within this one.
        if (t_walking > 0) {
            const std::lock_guard guard(_mutex);
            _passPending = true;
            return;
        }
        World::RequireNoneHeld("a delegation pass was run");
        const Walking walking;
        RunPass();
        // What the pass put off while it had ReplicaManager3's world: functions queued with
        // World::WhenNoneHeld (an entity's OnSimulatorChanged), state-change handlers, teardowns.
        World::Settle();
        DeliverNotices();
    }

    void DelegationManager::RunPass() {
        FW_PROFILE_SCOPE_N("Replication::DelegationPass");
        // Letting go of an entity below must not run what this thread put off: that is promised to
        // run with nothing of the Framework's held, and the pass holds ReplicaManager3's world.
        // Declared first, so it outlives the guard; UpdateNow settles afterwards.
        const World::HoldOff heldOff;
        // One pass at a time, and the connections stay as they are for it: the world comes before
        // the entities the pass then takes one by one.
        const MafiaNet::ReplicaManager3::WorldGuard pass(*_manager);
        World &world       = _manager->GetWorld();
        MafiaNet::Time now = 0;
        bool groundXY      = false;
        {
            const std::lock_guard guard(_mutex);
            _passPending = false;
            _lastPass    = MafiaNet::GetTime();
            now          = _lastPass;
            groundXY     = _groundXY;
        }
        CollectCandidates();

        // Two passes over the delegated entities: the first collects them and charges every
        // simulator for what it is already carrying, the second elects. Loads have to be complete
        // before the first election decision, or the entity that happens to be walked first sees an
        // empty budget and a client can be handed its whole capacity in one pass.
        _scan.clear();
        for (const uint64_t id : world.IdsOfType(0, true)) {
            const ConstRef<NetworkEntity> entity = world.Read<NetworkEntity>(id);
            if (!entity || entity->GetDelegationPolicy() == nullptr) {
                continue;
            }
            _scan.push_back(id);
            if (entity->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                continue;
            }
            if (DelegationCandidate *candidate = FindCandidate(entity->ownerGUID)) {
                candidate->load += entity->GetDelegationPolicy()->weight;
            }
        }

        uint32_t simulated = 0;
        uint32_t dormant   = 0;
        uint64_t refusals  = 0;
        uint64_t forced    = 0;
        std::vector<MafiaNet::PeerGuid> streaming;

        for (const uint64_t id : _scan) {
            Entry entry;
            {
                const std::lock_guard guard(_mutex);
                entry = _entries[id];
            }
            // A client that streamed the entity out can no longer withdraw its decline, so the decline goes with the entity.
            // Asked of the connections before the entity is taken: their lock comes first.
            if (!entry.declined.empty()) {
                streaming = _manager->StreamingPeers(id);
                const std::lock_guard guard(_mutex);
                Entry &stored = _entries[id];
                std::erase_if(stored.declined, [&](MafiaNet::PeerGuid guid) {
                    return !std::ranges::contains(streaming, guid);
                });
                entry = stored;
            }

            const Ref<NetworkEntity> entity = world.Write<NetworkEntity>(id);
            if (!entity || entity->GetDelegationPolicy() == nullptr) {
                continue;
            }
            const DelegationPolicy policy    = entity->GetDelegationPolicy()->Normalized();
            const MafiaNet::PeerGuid current = entity->ownerGUID;
            MafiaNet::PeerGuid desired       = MafiaNet::UNASSIGNED_PEER_GUID;
            DelegationChange change          = DelegationChange::Dormant;
            const auto setRefusingSince      = [&](MafiaNet::Time since) {
                if (entry.refusingSince == since) {
                    return;
                }
                entry.refusingSince = since;
                const std::lock_guard guard(_mutex);
                _entries[id].refusingSince = since;
            };

            if (entry.pinned) {
                // A pin overrides distance entirely, but not existence: a pin naming a client that
                // is not here leaves the entity dormant until that client comes back.
                desired = FindCandidate(entry.pinnedTo) != nullptr ? entry.pinnedTo : MafiaNet::UNASSIGNED_PEER_GUID;
            }
            else {
                desired = Elect(policy, entity->position, entity->GetVirtualWorld(), current, _candidates, groundXY, entry.declined);
            }

            if (desired == current) {
                setRefusingSince(0);
                (current != MafiaNet::UNASSIGNED_PEER_GUID ? simulated : dormant)++;
                continue;
            }

            // An entity mid-action gets to say "not now". It is asked only when something is
            // actually about to change, and only while it has an owner to refuse on behalf of --
            // an entity nobody is simulating has nothing to interrupt. A declining owner gave it up.
            const bool declinedByCurrent = std::ranges::contains(entry.declined, current);
            if (current != MafiaNet::UNASSIGNED_PEER_GUID && !declinedByCurrent && !entity->CanReleaseSimulation()) {
                if (entry.refusingSince == 0) {
                    setRefusingSince(now);
                }
                if (now - entry.refusingSince < static_cast<MafiaNet::Time>(policy.graceMs)) {
                    ++refusals;
                    ++simulated;
                    continue;
                }
                // The grace is what stops a permanently busy entity pinning itself to a client that
                // has walked away. Past it the handover happens whatever the entity says.
                ++forced;
            }
            setRefusingSince(0);

            if (current == MafiaNet::UNASSIGNED_PEER_GUID) {
                change = DelegationChange::Acquired;
            }
            else if (desired != MafiaNet::UNASSIGNED_PEER_GUID) {
                change = DelegationChange::Migrated;
            }

            // Charge and refund before the assignment, so the rest of this pass elects against the
            // load this decision creates rather than the one it started with.
            if (DelegationCandidate *loser = FindCandidate(current)) {
                loser->load = loser->load >= policy.weight ? loser->load - policy.weight : 0;
            }
            if (DelegationCandidate *winner = FindCandidate(desired)) {
                winner->load += policy.weight;
            }

            Assign(*entity, desired, change);
            (desired != MafiaNet::UNASSIGNED_PEER_GUID ? simulated : dormant)++;
        }

        const std::lock_guard guard(_mutex);
        _stats.simulated = simulated;
        _stats.dormant   = dormant;
        _stats.refusals += refusals;
        _stats.forced += forced;
    }

    uint32_t DelegationManager::LoadOf(MafiaNet::PeerGuid guid) const {
        uint32_t load = 0;
        if (!_manager || guid == MafiaNet::UNASSIGNED_PEER_GUID) {
            return 0;
        }
        World &world = _manager->GetWorld();
        for (const uint64_t id : world.IdsOfType(0, true)) {
            const ConstRef<NetworkEntity> entity = world.Read<NetworkEntity>(id);
            const DelegationPolicy *policy       = entity ? entity->GetDelegationPolicy() : nullptr;
            if (policy != nullptr && entity->ownerGUID == guid) {
                load += policy->weight;
            }
        }
        return load;
    }

    uint32_t DelegationManager::HeadroomOf(MafiaNet::PeerGuid guid) const {
        const uint32_t capacity = CapacityOf(guid);
        if (capacity == 0) {
            return std::numeric_limits<uint32_t>::max();
        }
        const uint32_t load = LoadOf(guid);
        return load >= capacity ? 0 : capacity - load;
    }
} // namespace Framework::Networking::Replication
