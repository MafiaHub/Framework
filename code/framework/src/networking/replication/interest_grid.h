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
        void CollectVisible(NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, std::unordered_set<NetworkEntity *> &out);

      private:
        // Range query on the configured ground plane; the set dedupes per-cell hits.
        void QueryRadius(const glm::vec3 &center, float radius, std::unordered_set<NetworkEntity *> &out);

        // Second ground-plane axis (the first is always X): Y when _groundXY, else Z.
        float GroundV(const glm::vec3 &p) const {
            return _groundXY ? p.y : p.z;
        }

        const std::unordered_set<NetworkEntity *> *OwnedBy(MafiaNet::PeerGuid guid) const;
        const std::unordered_set<NetworkEntity *> &AlwaysVisible() const {
            return _alwaysVisible;
        }

        bool _ready          = false;
        // One-shot guard so the "queried before a rebuild" diagnostic is logged once, not per query.
        bool _warnedNotReady = false;
        float _cellSize      = 100.0f;
        float _min           = -10000.0f;
        float _max           = 10000.0f;
        bool _groundXY       = false; // false=XZ (Y-up), true=XY (Z-up); see SetGroundPlaneXY
        uint32_t _generation = 0;
        GridSectorizer _grid;
        std::unordered_map<MafiaNet::PeerGuid, std::unordered_set<NetworkEntity *>> _ownedByGuid;
        std::unordered_set<NetworkEntity *> _alwaysVisible;
        // Entities currently in the index; grid hits are filtered through it (see class comment).
        std::unordered_set<NetworkEntity *> _live;
        // Scratch containers reused across queries so the per-connection-per-tick hot path performs
        // no steady-state allocations (clear() keeps the buckets/capacity).
        DataStructures::List<void *> _queryHits;
        std::unordered_set<NetworkEntity *> _inRange;
    };
} // namespace Framework::Networking::Replication
