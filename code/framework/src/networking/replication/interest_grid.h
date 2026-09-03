/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "network_entity.h"

#include <mafianet/DS_List.h>
#include <mafianet/GridSectorizer.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Framework::Networking::Replication {
    // Server-side spatial interest index, split out of ReplicationManager so the relevance data and
    // the queries over it live in one place. GridSectorizer has no incremental removal, so it is
    // rebuilt from scratch each tick: BeginRebuild() then Insert() per live entity. The owned-by-guid
    // and always-visible sets are rebuilt alongside it (kept authoritative against direct
    // ownerGUID/streaming writes) and give ReplicationConnection::QueryReplicaList O(1) membership.
    //
    // The grid itself cannot drop an entry between rebuilds (the vendored GridSectorizer compiles
    // without removal support), so a parallel live-entity set is the deletion authority: Remove()
    // drops the entity there and every query filters grid hits through it, which is what keeps an
    // intra-tick DestroyEntity from handing out a dangling pointer.
    class InterestGrid final {
      public:
        // Spatial index extent. Defaults cover a 20km² map at 100m cells (~40k cells). Pick bounds
        // that enclose the playable area; entities outside clamp to edge cells (still found by radius
        // queries, just less precisely). Takes effect on the next BeginRebuild().
        void Configure(float cellSize, float worldMin, float worldMax);

        // Select the horizontal plane for relevance: false (default) uses XZ (Y-up engines); true uses
        // XY (Z-up engines, e.g. Mafia 2). Streaming distance must be measured on the ground plane, so
        // a Z-up game whose height is Z has to set this or height bleeds into the radius check.
        void SetGroundPlaneXY(bool groundXY) {
            _groundXY = groundXY;
        }

        // Hysteresis: an already-relevant entity streams in at its range and out only past
        // range + margin. Default 0 = a binary boundary.
        void SetStreamOutMargin(float margin) {
            _streamOutMargin = margin > 0.0f ? margin : 0.0f;
        }

        // Seconds of viewer velocity added as a second focus point. Relevance takes the nearest
        // focus, so this only ever widens the set. Default 0 = the avatar is the only focus.
        void SetLookaheadSeconds(float seconds) {
            _lookaheadSeconds = seconds > 0.0f ? seconds : 0.0f;
        }

        // Cap on in-range entities of one type per viewer, nearest first (0/unset = uncapped).
        // Owned, always-visible, dependency and viewer entities are additive and uncounted.
        void SetBudget(uint32_t typeId, uint32_t maxCount);

        // Start a fresh rebuild: (re)initialise the grid if needed, then clear it and the indices.
        void BeginRebuild();
        // Add one live entity to the grid and the owned/always-visible indices.
        void Insert(NetworkEntity *entity);
        // Drop an entity from the indices so an intra-tick delete can't dangle before the next rebuild.
        void Remove(NetworkEntity *entity);

        // Monotonic counter bumped whenever the index contents change (rebuild or removal).
        // Consumers cache query results against it: ReplicaManager3 re-runs QueryReplicaList on every
        // RakPeer::Receive() call, but the index only changes once per tick, so an unchanged
        // generation means the cached result is still exact.
        uint32_t Generation() const {
            return _generation;
        }

        // Fill `out` with the entities relevant to `viewer`: in range and dimension, owned, or
        // always-visible. The home of the server's relevance rule.
        //
        // `previous` is the viewer's relevant set from the last computation and drives both
        // hysteresis rules (stream-out margin, sticky ranking). Membership only — the pointers are
        // compared, never dereferenced, so a stale entry is harmless.
        void CollectVisible(NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, const std::unordered_set<NetworkEntity *> &previous, std::unordered_set<NetworkEntity *> &out);

      private:
        // An in-range candidate and its budget-ranking distance (sticky discount folded in).
        struct Ranked {
            NetworkEntity *entity;
            float rankDistSq;
        };

        // Live entities whose cell overlaps the query box. No exact distance test: CollectVisible
        // applies each candidate's own range.
        void GatherCandidates(const glm::vec3 &center, float radius, std::unordered_set<NetworkEntity *> &out);

        // Second ground-plane axis (the first is always X): Y when _groundXY, else Z.
        float GroundV(const glm::vec3 &p) const {
            return _groundXY ? p.y : p.z;
        }

        // Ground-plane distance² to the nearest of the current focus points.
        float NearestFocusDistSq(const glm::vec3 &p) const;

        uint32_t BudgetFor(uint32_t typeId) const;

        const std::unordered_set<NetworkEntity *> *OwnedBy(MafiaNet::PeerGuid guid) const;
        const std::unordered_set<NetworkEntity *> &AlwaysVisible() const {
            return _alwaysVisible;
        }

        // An entity already in the set ranks at 72% of its true distance², so a challenger must be
        // ~15% nearer to evict it. Ranking only; never admits what the range check rejected.
        static constexpr float kStickyDiscount = 0.72f;

        bool _ready          = false;
        // One-shot guard so the "queried before a rebuild" diagnostic is logged once, not per query.
        bool _warnedNotReady = false;
        float _cellSize      = 100.0f;
        float _min           = -10000.0f;
        float _max           = 10000.0f;
        bool _groundXY       = false; // false=XZ (Y-up), true=XY (Z-up); see SetGroundPlaneXY
        float _streamOutMargin  = 0.0f;
        float _lookaheadSeconds = 0.0f;
        // Widest per-entity range in the current rebuild; the grid query must reach that far.
        float _maxEntityRange = 0.0f;
        uint32_t _generation = 0;
        GridSectorizer _grid;
        std::unordered_map<uint32_t, uint32_t> _budgets;
        std::unordered_map<MafiaNet::PeerGuid, std::unordered_set<NetworkEntity *>> _ownedByGuid;
        std::unordered_set<NetworkEntity *> _alwaysVisible;
        // Entities currently in the index; grid hits are filtered through it (see class comment).
        std::unordered_set<NetworkEntity *> _live;
        // Scratch containers reused across queries so the per-connection-per-tick hot path performs
        // no steady-state allocations (clear() keeps the buckets/capacity).
        DataStructures::List<void *> _queryHits;
        std::unordered_set<NetworkEntity *> _candidates;
        std::vector<glm::vec3> _focus;
        std::unordered_map<uint32_t, std::vector<Ranked>> _ranked;
        std::vector<NetworkEntity *> _dependencyScan;
    };
} // namespace Framework::Networking::Replication
