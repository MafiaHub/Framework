/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "interest_grid.h"

#include <mafianet/DS_List.h>

namespace Framework::Networking::Replication {
    namespace {
        // Half-extent of a point entity's bounding box in the spatial index. GridSectorizer requires
        // min < max (it asserts otherwise), so a point is inserted as a tiny box around its position.
        constexpr float kPointEpsilon = 0.01f;
    } // namespace

    void InterestGrid::Configure(float cellSize, float worldMin, float worldMax) {
        _cellSize = cellSize;
        _min      = worldMin;
        _max      = worldMax;
        _ready    = false; // re-initialised on next BeginRebuild()
    }

    void InterestGrid::BeginRebuild() {
        if (!_ready) {
            _grid.Init(_cellSize, _cellSize, _min, _min, _max, _max);
            _ready = true;
        }
        _grid.Clear();
        _ownedByGuid.clear();
        _alwaysVisible.clear();
    }

    void InterestGrid::Insert(NetworkEntity *entity) {
        if (!entity) {
            return;
        }
        // GridSectorizer asserts on a zero-area entry, so insert a tiny box around the XZ position.
        _grid.AddEntry(entity, entity->position.x - kPointEpsilon, entity->position.z - kPointEpsilon, entity->position.x + kPointEpsilon, entity->position.z + kPointEpsilon);
        if (IsAssigned(entity->ownerGUID)) {
            _ownedByGuid[entity->ownerGUID].insert(entity);
        }
        if (entity->streaming.alwaysVisible) {
            _alwaysVisible.insert(entity);
        }
    }

    void InterestGrid::Remove(NetworkEntity *entity) {
        for (auto it = _ownedByGuid.begin(); it != _ownedByGuid.end();) {
            it->second.erase(entity);
            if (it->second.empty()) {
                it = _ownedByGuid.erase(it);
            }
            else {
                ++it;
            }
        }
        _alwaysVisible.erase(entity);
    }

    void InterestGrid::QueryRadius(const glm::vec3 &center, float radius, std::unordered_set<NetworkEntity *> &out) {
        if (!_ready) {
            return;
        }
        DataStructures::List<void *> hits;
        _grid.GetEntries(hits, center.x - radius, center.z - radius, center.x + radius, center.z + radius);

        const float radiusSq = radius * radius;
        for (unsigned i = 0; i < hits.Size(); ++i) {
            auto *entity = static_cast<NetworkEntity *>(hits[i]);
            if (!entity) {
                continue;
            }
            const glm::vec3 delta = entity->position - center;
            // 2D (XZ) distance check; entries spanning multiple cells can repeat — the set dedupes.
            if (delta.x * delta.x + delta.z * delta.z > radiusSq) {
                continue;
            }
            out.insert(entity);
        }
    }

    const std::unordered_set<NetworkEntity *> *InterestGrid::OwnedBy(PeerGuid guid) const {
        const auto it = _ownedByGuid.find(guid);
        return it != _ownedByGuid.end() ? &it->second : nullptr;
    }

    void InterestGrid::CollectVisible(NetworkEntity *viewer, PeerGuid viewerGUID, std::unordered_set<NetworkEntity *> &out) {
        if (!viewer) {
            return;
        }
        const auto observerWorld = viewer->GetVirtualWorld();

        std::unordered_set<NetworkEntity *> inRange;
        QueryRadius(viewer->position, viewer->streaming.range, inRange);

        // Owned and always-visible entities bypass range/dimension culling so they never drop out.
        const auto visible = [&](NetworkEntity *entity) {
            if (!entity || !entity->streaming.visible) {
                return false;
            }
            return entity->streaming.alwaysVisible || entity == viewer || entity->ownerGUID == viewerGUID || (MafiaNet::VirtualWorldsCanSee(entity->GetVirtualWorld(), observerWorld) && inRange.contains(entity));
        };

        const auto consider = [&](NetworkEntity *entity) {
            if (visible(entity)) {
                out.insert(entity);
            }
        };

        for (NetworkEntity *entity : inRange) {
            consider(entity);
        }
        if (const auto *owned = OwnedBy(viewerGUID)) {
            for (NetworkEntity *entity : *owned) {
                consider(entity);
            }
        }
        for (NetworkEntity *entity : AlwaysVisible()) {
            consider(entity);
        }
        consider(viewer);
    }
} // namespace Framework::Networking::Replication
