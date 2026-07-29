/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "interest_grid.h"

#include <logging/logger.h>
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
        _live.clear();
        ++_generation;
    }

    void InterestGrid::Insert(NetworkEntity *entity) {
        if (!entity) {
            return;
        }
        // GridSectorizer asserts on a zero-area entry, so insert a tiny box around the ground-plane
        // position (X and the configured second axis).
        const float v = GroundV(entity->position);
        _grid.AddEntry(entity, entity->position.x - kPointEpsilon, v - kPointEpsilon, entity->position.x + kPointEpsilon, v + kPointEpsilon);
        _live.insert(entity);
        if (entity->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
            _ownedByGuid[entity->ownerGUID].insert(entity);
        }
        if (entity->streaming.alwaysVisible) {
            _alwaysVisible.insert(entity);
        }
    }

    void InterestGrid::Remove(NetworkEntity *entity) {
        // The stale grid entry stays until the next rebuild; dropping the entity from _live is what
        // makes the queries skip it (see class comment).
        _live.erase(entity);
        ++_generation;
        // Fast path: the index was keyed on ownerGUID at the last rebuild, so the bucket is directly
        // addressable — unless game code rewrote ownerGUID since then, in which case sweep them all.
        const auto bucket = _ownedByGuid.find(entity->ownerGUID);
        if (bucket != _ownedByGuid.end() && bucket->second.erase(entity) != 0) {
            if (bucket->second.empty()) {
                _ownedByGuid.erase(bucket);
            }
        }
        else {
            for (auto it = _ownedByGuid.begin(); it != _ownedByGuid.end();) {
                it->second.erase(entity);
                if (it->second.empty()) {
                    it = _ownedByGuid.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
        _alwaysVisible.erase(entity);
    }

    void InterestGrid::QueryRadius(const glm::vec3 &center, float radius, std::unordered_set<NetworkEntity *> &out) {
        if (!_ready) {
            return;
        }
        const float centerV = GroundV(center);
        _queryHits.Clear(true, _FILE_AND_LINE_);
        _grid.GetEntries(_queryHits, center.x - radius, centerV - radius, center.x + radius, centerV + radius);

        const float radiusSq = radius * radius;
        for (unsigned i = 0; i < _queryHits.Size(); ++i) {
            auto *entity = static_cast<NetworkEntity *>(_queryHits[i]);
            // The grid can hold entries for entities destroyed since the last rebuild; only _live
            // pointers are safe to dereference.
            if (!entity || !_live.contains(entity)) {
                continue;
            }
            const float dx = entity->position.x - center.x;
            const float dv = GroundV(entity->position) - centerV;
            // 2D ground-plane distance; entries spanning multiple cells can repeat — the set dedupes.
            if (dx * dx + dv * dv > radiusSq) {
                continue;
            }
            out.insert(entity);
        }
    }

    const std::unordered_set<NetworkEntity *> *InterestGrid::OwnedBy(MafiaNet::PeerGuid guid) const {
        const auto it = _ownedByGuid.find(guid);
        return it != _ownedByGuid.end() ? &it->second : nullptr;
    }

    void InterestGrid::CollectVisible(NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, std::unordered_set<NetworkEntity *> &out, size_t *candidatesOut) {
        if (!viewer) {
            if (candidatesOut) {
                *candidatesOut = 0;
            }
            return;
        }
        if (!_ready) {
            if (!_warnedNotReady) {
                _warnedNotReady = true;
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("InterestGrid queried before its first RebuildInterest(); no entities will replicate. Call ReplicationManager::RebuildInterest() once per server tick.");
            }
            if (candidatesOut) {
                *candidatesOut = 0;
            }
            return;
        }
        const auto observerWorld = viewer->GetVirtualWorld();

        _inRange.clear();
        QueryRadius(viewer->position, viewer->streaming.range, _inRange);

        if (candidatesOut) {
            size_t candidates = _inRange.size();
            if (const auto *owned = OwnedBy(viewerGUID)) {
                candidates += owned->size();
            }
            candidates += AlwaysVisible().size();
            candidates += 1;
            *candidatesOut = candidates;
        }

        // A private entity (targetGUID set) is only ever relevant to its target connection, and for
        // that viewer the target stands in for the dimension gate.
        const auto targetMatch = [viewerGUID](const NetworkEntity *entity) {
            return entity->streaming.targetGUID == MafiaNet::UNASSIGNED_PEER_GUID || entity->streaming.targetGUID == viewerGUID;
        };
        const auto targeted = [](const NetworkEntity *entity) {
            return entity->streaming.targetGUID != MafiaNet::UNASSIGNED_PEER_GUID;
        };

        // In-range entities still face the dimension check; owned entities and the viewer itself
        // bypass range and dimension culling so they never drop out. Always-visible entities bypass
        // range only, keeping the interest set in agreement with QuerySerialization's dimension gate.
        for (NetworkEntity *entity : _inRange) {
            if (entity->streaming.visible && targetMatch(entity) && (entity == viewer || entity->ownerGUID == viewerGUID || targeted(entity) || MafiaNet::VirtualWorldsCanSee(entity->GetVirtualWorld(), observerWorld))) {
                out.insert(entity);
            }
        }
        if (const auto *owned = OwnedBy(viewerGUID)) {
            for (NetworkEntity *entity : *owned) {
                if (entity->streaming.visible && targetMatch(entity)) {
                    out.insert(entity);
                }
            }
        }
        for (NetworkEntity *entity : AlwaysVisible()) {
            if (entity->streaming.visible && targetMatch(entity) && (targeted(entity) || MafiaNet::VirtualWorldsCanSee(entity->GetVirtualWorld(), observerWorld))) {
                out.insert(entity);
            }
        }
        if (viewer->streaming.visible && targetMatch(viewer)) {
            out.insert(viewer);
        }
    }
} // namespace Framework::Networking::Replication
