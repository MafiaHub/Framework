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
    class InterestGrid {
      public:
        // Spatial index extent. Defaults cover a 20km² map at 100m cells (~40k cells). Pick bounds
        // that enclose the playable area; entities outside clamp to edge cells (still found by radius
        // queries, just less precisely). Takes effect on the next BeginRebuild().
        void Configure(float cellSize, float worldMin, float worldMax);

        // Start a fresh rebuild: (re)initialise the grid if needed, then clear it and the indices.
        void BeginRebuild();
        // Add one live entity to the grid and the owned/always-visible indices.
        void Insert(NetworkEntity *entity);
        // Drop an entity from the indices so an intra-tick delete can't dangle before the next rebuild.
        void Remove(NetworkEntity *entity);

        // Fill `out` with the entities relevant to `viewer`: in range and dimension, owned, or
        // always-visible. The home of the server's relevance rule.
        void CollectVisible(NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, std::unordered_set<NetworkEntity *> &out);

      private:
        // Range query on the XZ plane; the set dedupes per-cell hits.
        void QueryRadius(const glm::vec3 &center, float radius, std::unordered_set<NetworkEntity *> &out);

        const std::unordered_set<NetworkEntity *> *OwnedBy(MafiaNet::PeerGuid guid) const;
        const std::unordered_set<NetworkEntity *> &AlwaysVisible() const {
            return _alwaysVisible;
        }

        bool _ready     = false;
        float _cellSize = 100.0f;
        float _min      = -10000.0f;
        float _max      = 10000.0f;
        GridSectorizer _grid;
        std::unordered_map<MafiaNet::PeerGuid, std::unordered_set<NetworkEntity *>> _ownedByGuid;
        std::unordered_set<NetworkEntity *> _alwaysVisible;
    };
} // namespace Framework::Networking::Replication
