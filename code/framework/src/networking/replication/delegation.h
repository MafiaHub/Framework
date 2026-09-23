/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "network_entity.h"

#include <mafianet/GetTime.h>
#include <mafianet/types.h>

#include <function2/function2.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Framework::Networking::Replication {
    class ReplicationManager;

    // Why an entity's simulator changed, for the callback a game hangs off this.
    enum class DelegationChange : uint8_t {
        // Nobody was simulating it and a client took it on.
        Acquired,
        // One client handed it to another without it ever going dormant.
        Migrated,
        // Its simulator went away or moved out of range and nobody replaced it. The entity is now
        // server-owned ("dormant"): it still exists and still replicates, but no client is running it.
        Dormant,
    };

    // What an entity tells the delegation manager about how it wants to be simulated. Server-side
    // only, never replicated: the client learns it holds the simulation from ownership alone.
    //
    // Ranges are measured on the interest ground plane, between the entity and the candidate's
    // viewer entity (a player's avatar). They are deliberately two numbers rather than one: with a
    // single boundary a simulator standing exactly at the limit is handed back and forth on every
    // pass, and the entity stutters for as long as the player stands there.
    struct DelegationPolicy {
        // How close a client has to be to take the entity on.
        float acquireRange = 100.0f;

        // How far the client that is already simulating it may go before losing it. Must be >=
        // acquireRange for the hysteresis to mean anything; the manager clamps it if it is not.
        float releaseRange = 140.0f;

        // What this entity costs against a client's simulation budget. A budget is not a network
        // number -- it is whatever the game engine can actually run per machine -- so entities that
        // cost more to run can say so.
        uint32_t weight = 1;

        // How long a refusal to hand over (NetworkEntity::CanReleaseSimulation) is honoured before
        // the handover happens anyway. Without a deadline an entity that is permanently busy would
        // pin itself to a client that has walked to the other end of the map.
        uint32_t graceMs = 5000;

        // Whether a candidate must be in the entity's virtual world. Off only for an entity that is
        // deliberately cross-world; the default is the safe answer.
        bool requireSameVirtualWorld = true;

        // Clamped copy: release can never be nearer than acquire, and neither can be negative.
        DelegationPolicy Normalized() const {
            DelegationPolicy out = *this;
            out.acquireRange     = acquireRange > 0.0f ? acquireRange : 0.0f;
            out.releaseRange     = releaseRange > out.acquireRange ? releaseRange : out.acquireRange;
            return out;
        }
    };

    // One candidate simulator, as the election sees it: where its player is, which world they are
    // in, and how much simulation they are already carrying.
    struct DelegationCandidate {
        MafiaNet::PeerGuid guid = MafiaNet::UNASSIGNED_PEER_GUID;
        glm::vec3 position      = glm::vec3(0.0f);
        MafiaNet::VirtualWorldId virtualWorld = 0;

        // Policy weight this client is already simulating, and the most it may carry (0 = unlimited).
        uint32_t load     = 0;
        uint32_t capacity = 0;
    };

    // Server-side election of which client simulates which entity.
    //
    // Some things a server owns cannot be simulated by the server: an NPC that has to walk around a
    // world the server does not have, a physics prop nobody has the physics for. The answer every
    // multiplayer mod of a singleplayer game arrives at is the same -- pick one client, let it run
    // the thing with the real engine, and relay what it reports to everybody else. MTA calls that
    // client the syncer and re-elects it every 500 ms inside a fixed radius; FiveM migrates entity
    // ownership to the nearest of the clients an entity is relevant to. This is that mechanism,
    // once, for every game built on the framework.
    //
    // It is deliberately built on the ownership the replication layer already has rather than beside
    // it: the elected client becomes the entity's owner, so it inherits the whole authority model --
    // its updates travel upstream and are relayed, the server's overrides reach it through
    // ForceState, stale updates from a revoked simulator are dropped by the state epoch, and the
    // pose ordering is reset on the handover. A game's own code asks `IsOwner()` and nothing else.
    //
    // The entity opts in by overriding NetworkEntity::GetDelegationPolicy. Nothing else is required;
    // an entity that does not is untouched by any of this.
    class DelegationManager final {
      public:
        // Sticky ranking: the client already simulating an entity is ranked at 72% of its true
        // distance, so a challenger has to be about 15% nearer before the entity moves. The same
        // constant and the same reason as InterestGrid's, which is where this is borrowed from --
        // two players walking past an NPC in opposite directions otherwise trade it every pass.
        static constexpr float kStickyDiscount = 0.72f;

        /**
         * Who should be simulating an entity, given where it is and who is near it.
         *
         * The whole election rule, as a pure function of its inputs: no manager, no entity, no
         * clock. It is public because it is the part worth testing on its own and the part a game
         * may want to ask about without waiting for a pass -- `UpdateNow` is the glue that collects
         * the inputs, applies the answer and keeps the bookkeeping.
         *
         * `current` is the guid simulating it now, or UNASSIGNED for dormant. The answer is the guid
         * that should simulate it, which may be `current` (keep it), another candidate (hand it
         * over), or UNASSIGNED (let it go dormant).
         *
         * Two asymmetries are deliberate and are what stop an entity being traded back and forth:
         * the incumbent is measured against `releaseRange` while a challenger has to be inside
         * `acquireRange`, and the incumbent's distance is discounted by `kStickyDiscount` before the
         * comparison. An incumbent is also never dropped for being over capacity -- a budget lowered
         * at runtime stops new work arriving rather than stuttering what is already running.
         */
        static MafiaNet::PeerGuid Elect(const DelegationPolicy &policy, const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld, MafiaNet::PeerGuid current, const std::vector<DelegationCandidate> &candidates, bool groundXY);

        // Server only; a client-side manager leaves this untouched.
        void Init(ReplicationManager *manager, bool isServer);

        // Milliseconds between election passes. 500 by default, which is MTA's number for the same
        // job: often enough that walking into range feels immediate, rarely enough that the walk
        // over every delegated entity is not a per-tick cost.
        void SetUpdateInterval(uint32_t intervalMs) {
            _intervalMs = intervalMs;
        }

        // Match the interest grid's ground plane: false (default) measures on XZ (Y-up engines),
        // true on XY (Z-up). ReplicationManager::SetInterestGroundPlaneXY forwards to this, so a
        // game that configures interest has configured delegation too.
        void SetGroundPlaneXY(bool groundXY) {
            _groundXY = groundXY;
        }

        // Total weight one client may simulate at once, 0 for unlimited (the default). A client at
        // its budget keeps what it has and takes nothing new, so the overflow goes to the next
        // nearest client rather than being dropped.
        void SetClientCapacity(uint32_t maxWeight) {
            _defaultCapacity = maxWeight;
        }

        // Per-client override of the above, for a machine that has said what it can take.
        void SetClientCapacity(MafiaNet::PeerGuid guid, uint32_t maxWeight);
        void ClearClientCapacity(MafiaNet::PeerGuid guid);

        // Force this entity onto one client and keep it there, whatever the ranges say. What MTA's
        // persistent syncer is for: a scripted scene whose actor must be run by the player it is
        // being played to. Passing UNASSIGNED pins it to the server, i.e. keeps it dormant.
        void Pin(NetworkEntity *entity, MafiaNet::PeerGuid guid);
        void Unpin(NetworkEntity *entity);
        bool IsPinned(const NetworkEntity *entity) const;
        MafiaNet::PeerGuid PinnedTo(const NetworkEntity *entity) const;

        // One election pass, at most every interval. Driven from NetworkPeer::Update after the
        // interest rebuild, so it reads the positions this tick settled.
        void Update();

        // An immediate pass, ignoring the interval. Used when the world changed under the schedule:
        // a client dropped, or the game wants an entity placed now.
        void UpdateNow();

        // A client went away: everything it was simulating is re-elected on the spot rather than
        // waiting out the interval, because until then those entities have an owner that cannot
        // report anything.
        void OnClientDisconnected(MafiaNet::PeerGuid guid);

        // Drop the bookkeeping for an entity that no longer exists. Called by ReplicationManager;
        // games do not need to.
        void OnEntityDestroyed(uint64_t networkId);

        // Raised after the ownership change has been applied and the forced state pushed, so a
        // handler sees the entity in its new arrangement. `previous` is UNASSIGNED when the entity
        // was dormant, and `current` is UNASSIGNED when it just became so.
        //
        // Handles rather than one slot, for the same reason state-bag subscriptions are handles: a
        // game has more than one kind of delegated entity, and the second feature to want this must
        // not silently replace the first. Returns kInvalidSimulatorHandle for an empty callback.
        using SimulatorChangedHandle = uint32_t;
        static constexpr SimulatorChangedHandle kInvalidSimulatorHandle = 0;

        SimulatorChangedHandle AddSimulatorChangedHandler(fu2::function<void(NetworkEntity *, MafiaNet::PeerGuid, MafiaNet::PeerGuid, DelegationChange) const> callback);
        void RemoveSimulatorChangedHandler(SimulatorChangedHandle handle);

        // What one client is currently carrying, in policy weight.
        uint32_t LoadOf(MafiaNet::PeerGuid guid) const;

        // How much of that client's budget is left, or UINT32_MAX when it is unlimited.
        uint32_t HeadroomOf(MafiaNet::PeerGuid guid) const;

        struct Stats {
            // Delegated entities currently being simulated by some client.
            uint32_t simulated = 0;
            // Delegated entities nobody is near enough to run.
            uint32_t dormant = 0;
            // Handovers and acquisitions since the server started.
            uint64_t handovers = 0;
            // Passes where a handover was wanted and the entity refused it.
            uint64_t refusals = 0;
            // Handovers forced past a refusal because the grace ran out.
            uint64_t forced = 0;
        };
        const Stats &GetStats() const {
            return _stats;
        }

      private:
        // Per-entity bookkeeping the entity itself has no business carrying.
        struct Entry {
            MafiaNet::PeerGuid pinnedTo = MafiaNet::UNASSIGNED_PEER_GUID;
            bool pinned                 = false;
            // When this entity first refused a handover it is still refusing. Zero when it is not.
            MafiaNet::Time refusingSince = 0;
        };

        // Snapshots the viewers once per pass, so the walk over entities does not re-resolve them
        // per entity.
        void CollectCandidates();
        DelegationCandidate *FindCandidate(MafiaNet::PeerGuid guid);

        // Apply an elected result to one entity: ownership, forced state, bookkeeping, callback.
        void Assign(NetworkEntity *entity, MafiaNet::PeerGuid next, DelegationChange change);

        // The entry for an entity, created on demand. Entries are keyed by NetworkID rather than by
        // pointer: an entity can be destroyed and its address reused between passes.
        Entry &EntryFor(const NetworkEntity *entity);
        const Entry *FindEntry(const NetworkEntity *entity) const;

        ReplicationManager *_manager = nullptr;
        bool _isServer               = false;
        bool _groundXY               = false;
        uint32_t _intervalMs         = 500;
        uint32_t _defaultCapacity    = 0;
        MafiaNet::Time _lastPass     = 0;

        std::vector<DelegationCandidate> _candidates;
        std::unordered_map<MafiaNet::PeerGuid, uint32_t> _capacityOverrides;
        std::unordered_map<uint64_t, Entry> _entries;
        // Rebuilt per pass; the entities a pass has to look at.
        std::vector<NetworkEntity *> _scan;
        // Keyed by handle, so a handler that unsubscribes during dispatch cannot invalidate what the
        // dispatch is walking.
        std::unordered_map<SimulatorChangedHandle, fu2::function<void(NetworkEntity *, MafiaNet::PeerGuid, MafiaNet::PeerGuid, DelegationChange) const>> _simulatorChanged;
        SimulatorChangedHandle _nextSimulatorHandle = kInvalidSimulatorHandle;
        // Scratch for one dispatch, reused so a handover allocates nothing.
        std::vector<SimulatorChangedHandle> _dispatch;
        Stats _stats;
    };
} // namespace Framework::Networking::Replication
