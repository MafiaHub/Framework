/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "delegation.h"

#include "replication_manager.h"

#include <logging/logger.h>

#include <mafianet/VirtualWorld.h>

#include <limits>

namespace Framework::Networking::Replication {
    void DelegationManager::Init(ReplicationManager *manager, bool isServer) {
        _manager  = manager;
        _isServer = isServer;
    }

    void DelegationManager::SetClientCapacity(MafiaNet::PeerGuid guid, uint32_t maxWeight) {
        if (guid == MafiaNet::UNASSIGNED_PEER_GUID) {
            return;
        }
        _capacityOverrides[guid] = maxWeight;
    }

    void DelegationManager::ClearClientCapacity(MafiaNet::PeerGuid guid) {
        _capacityOverrides.erase(guid);
    }

    void DelegationManager::Pin(NetworkEntity *entity, MafiaNet::PeerGuid guid) {
        if (!entity || !_isServer) {
            return;
        }
        Entry &entry  = EntryFor(entity);
        entry.pinned  = true;
        entry.pinnedTo = guid;
        // Immediately, rather than at the next pass: a pin is an instruction, and a script that
        // pins an entity and then talks to it expects the simulator to already be the one it named.
        UpdateNow();
    }

    void DelegationManager::Unpin(NetworkEntity *entity) {
        if (!entity) {
            return;
        }
        const auto it = _entries.find(entity->GetNetworkID());
        if (it == _entries.end()) {
            return;
        }
        it->second.pinned   = false;
        it->second.pinnedTo = MafiaNet::UNASSIGNED_PEER_GUID;
    }

    bool DelegationManager::IsPinned(const NetworkEntity *entity) const {
        const Entry *entry = FindEntry(entity);
        return entry != nullptr && entry->pinned;
    }

    MafiaNet::PeerGuid DelegationManager::PinnedTo(const NetworkEntity *entity) const {
        const Entry *entry = FindEntry(entity);
        return entry != nullptr && entry->pinned ? entry->pinnedTo : MafiaNet::UNASSIGNED_PEER_GUID;
    }

    void DelegationManager::OnEntityDestroyed(uint64_t networkId) {
        _entries.erase(networkId);
    }

    void DelegationManager::OnClientDisconnected(MafiaNet::PeerGuid guid) {
        if (!_isServer || !_manager) {
            return;
        }
        // The dropped peer is still the owner of everything it was simulating, and an owner that is
        // gone can never report again. Hand those entities back to the server here, so they are
        // dormant rather than owned-by-nobody until the next pass, and drop any pin naming that
        // client -- a pin is to a player, and that player has left.
        _manager->ForEachEntity([&](NetworkEntity *entity) {
            if (entity == nullptr || entity->GetDelegationPolicy() == nullptr) {
                return;
            }
            const auto it = _entries.find(entity->GetNetworkID());
            if (it != _entries.end() && it->second.pinned && it->second.pinnedTo == guid) {
                it->second.pinned   = false;
                it->second.pinnedTo = MafiaNet::UNASSIGNED_PEER_GUID;
            }
            if (entity->ownerGUID == guid) {
                Assign(entity, MafiaNet::UNASSIGNED_PEER_GUID, DelegationChange::Dormant);
            }
        });
        // Deliberately no re-election here: this runs while the dropped peer's viewer may still be
        // in the viewer map, and a pass now would hand its entities straight back to it. The caller
        // runs one once the viewer is gone.
    }

    void DelegationManager::Update() {
        if (!_isServer || !_manager) {
            return;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (_intervalMs != 0 && _lastPass != 0 && now - _lastPass < static_cast<MafiaNet::Time>(_intervalMs)) {
            return;
        }
        UpdateNow();
    }

    namespace {
        // NetworkIDObject::GetNetworkID is logically const and not marked so by
        // the vendored header, and every read here is from a const entity.
        uint64_t IdOf(const NetworkEntity *entity) {
            return const_cast<NetworkEntity *>(entity)->GetNetworkID();
        }
    } // namespace

    DelegationManager::Entry &DelegationManager::EntryFor(const NetworkEntity *entity) {
        return _entries[IdOf(entity)];
    }

    const DelegationManager::Entry *DelegationManager::FindEntry(const NetworkEntity *entity) const {
        if (entity == nullptr) {
            return nullptr;
        }
        const auto it = _entries.find(IdOf(entity));
        return it != _entries.end() ? &it->second : nullptr;
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

    MafiaNet::PeerGuid DelegationManager::Elect(const DelegationPolicy &rawPolicy, const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld, MafiaNet::PeerGuid current, const std::vector<DelegationCandidate> &candidates, bool groundXY) {
        const DelegationPolicy policy = rawPolicy.Normalized();
        MafiaNet::PeerGuid desired    = MafiaNet::UNASSIGNED_PEER_GUID;
        float bestRank                = std::numeric_limits<float>::max();

        const DelegationCandidate *incumbent = nullptr;
        for (const DelegationCandidate &candidate : candidates) {
            if (candidate.guid != MafiaNet::UNASSIGNED_PEER_GUID && candidate.guid == current) {
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
                desired  = current;
                bestRank = distSq * kStickyDiscount;
            }
        }

        for (const DelegationCandidate &candidate : candidates) {
            if (candidate.guid == MafiaNet::UNASSIGNED_PEER_GUID || candidate.guid == current) {
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

    void DelegationManager::CollectCandidates() {
        _candidates.clear();
        _manager->ForEachViewer([&](MafiaNet::PeerGuid guid, NetworkEntity *viewer) {
            if (guid == MafiaNet::UNASSIGNED_PEER_GUID || viewer == nullptr) {
                return;
            }
            DelegationCandidate candidate;
            candidate.guid         = guid;
            candidate.position     = viewer->position;
            candidate.virtualWorld = viewer->GetVirtualWorld();
            const auto override    = _capacityOverrides.find(guid);
            candidate.capacity     = override != _capacityOverrides.end() ? override->second : _defaultCapacity;
            _candidates.push_back(candidate);
        });
    }

    void DelegationManager::Assign(NetworkEntity *entity, MafiaNet::PeerGuid next, DelegationChange change) {
        const MafiaNet::PeerGuid previous = entity->ownerGUID;
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

        entity->OnSimulatorChanged(previous, next);
        // A copy of the handle list, so a handler that subscribes or unsubscribes from inside the
        // dispatch does not rehash the map being walked.
        _dispatch.clear();
        _dispatch.reserve(_simulatorChanged.size());
        for (const auto &[handle, callback] : _simulatorChanged) {
            (void)callback;
            _dispatch.push_back(handle);
        }
        for (const SimulatorChangedHandle handle : _dispatch) {
            const auto it = _simulatorChanged.find(handle);
            if (it != _simulatorChanged.end() && it->second) {
                it->second(entity, previous, next, change);
            }
        }
        ++_stats.handovers;
    }

    DelegationManager::SimulatorChangedHandle DelegationManager::AddSimulatorChangedHandler(fu2::function<void(NetworkEntity *, MafiaNet::PeerGuid, MafiaNet::PeerGuid, DelegationChange) const> callback) {
        if (!callback) {
            return kInvalidSimulatorHandle;
        }
        const SimulatorChangedHandle handle = ++_nextSimulatorHandle;
        _simulatorChanged[handle]           = std::move(callback);
        return handle;
    }

    void DelegationManager::RemoveSimulatorChangedHandler(SimulatorChangedHandle handle) {
        _simulatorChanged.erase(handle);
    }

    void DelegationManager::UpdateNow() {
        if (!_isServer || !_manager) {
            return;
        }

        _lastPass = MafiaNet::GetTime();
        CollectCandidates();

        // Two passes over the delegated entities: the first collects them and charges every
        // simulator for what it is already carrying, the second elects. Loads have to be complete
        // before the first election decision, or the entity that happens to be walked first sees an
        // empty budget and a client can be handed its whole capacity in one pass.
        _scan.clear();
        _manager->ForEachEntity([&](NetworkEntity *entity) {
            if (entity == nullptr || entity->GetDelegationPolicy() == nullptr) {
                return;
            }
            _scan.push_back(entity);
            if (entity->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                return;
            }
            if (DelegationCandidate *candidate = FindCandidate(entity->ownerGUID)) {
                candidate->load += entity->GetDelegationPolicy()->weight;
            }
        });

        const MafiaNet::Time now = _lastPass;
        uint32_t simulated       = 0;
        uint32_t dormant         = 0;

        for (NetworkEntity *entity : _scan) {
            const DelegationPolicy policy     = entity->GetDelegationPolicy()->Normalized();
            const MafiaNet::PeerGuid current  = entity->ownerGUID;
            Entry &entry                      = EntryFor(entity);
            MafiaNet::PeerGuid desired        = MafiaNet::UNASSIGNED_PEER_GUID;
            DelegationChange change           = DelegationChange::Dormant;

            if (entry.pinned) {
                // A pin overrides distance entirely, but not existence: a pin naming a client that
                // is not here leaves the entity dormant until that client comes back.
                desired = FindCandidate(entry.pinnedTo) != nullptr ? entry.pinnedTo : MafiaNet::UNASSIGNED_PEER_GUID;
            }
            else {
                desired = Elect(policy, entity->position, entity->GetVirtualWorld(), current, _candidates, _groundXY);
            }

            if (desired == current) {
                entry.refusingSince = 0;
                (current != MafiaNet::UNASSIGNED_PEER_GUID ? simulated : dormant)++;
                continue;
            }

            // An entity mid-action gets to say "not now". It is asked only when something is
            // actually about to change, and only while it has an owner to refuse on behalf of --
            // an entity nobody is simulating has nothing to interrupt.
            if (current != MafiaNet::UNASSIGNED_PEER_GUID && !entity->CanReleaseSimulation()) {
                if (entry.refusingSince == 0) {
                    entry.refusingSince = now;
                }
                if (now - entry.refusingSince < static_cast<MafiaNet::Time>(policy.graceMs)) {
                    ++_stats.refusals;
                    ++simulated;
                    continue;
                }
                // The grace is what stops a permanently busy entity pinning itself to a client that
                // has walked away. Past it the handover happens whatever the entity says.
                ++_stats.forced;
            }
            entry.refusingSince = 0;

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

            Assign(entity, desired, change);
            (desired != MafiaNet::UNASSIGNED_PEER_GUID ? simulated : dormant)++;
        }

        _stats.simulated = simulated;
        _stats.dormant   = dormant;
    }

    uint32_t DelegationManager::LoadOf(MafiaNet::PeerGuid guid) const {
        uint32_t load = 0;
        if (!_manager || guid == MafiaNet::UNASSIGNED_PEER_GUID) {
            return 0;
        }
        _manager->ForEachEntity([&](NetworkEntity *entity) {
            const DelegationPolicy *policy = entity != nullptr ? entity->GetDelegationPolicy() : nullptr;
            if (policy != nullptr && entity->ownerGUID == guid) {
                load += policy->weight;
            }
        });
        return load;
    }

    uint32_t DelegationManager::HeadroomOf(MafiaNet::PeerGuid guid) const {
        const auto override    = _capacityOverrides.find(guid);
        const uint32_t capacity = override != _capacityOverrides.end() ? override->second : _defaultCapacity;
        if (capacity == 0) {
            return std::numeric_limits<uint32_t>::max();
        }
        const uint32_t load = LoadOf(guid);
        return load >= capacity ? 0 : capacity - load;
    }
} // namespace Framework::Networking::Replication
