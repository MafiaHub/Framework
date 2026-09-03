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

#include <algorithm>
#include <limits>

namespace Framework::Networking::Replication {
    namespace {
        // Half-extent of a point entity's bounding box in the spatial index. GridSectorizer requires
        // min < max (it asserts otherwise), so a point is inserted as a tiny box around its position.
        constexpr float kPointEpsilon = 0.01f;
        // Under this the look-ahead point sits inside the avatar's own bubble, so the second grid
        // query would be pure cost.
        constexpr float kMinLookaheadDist = 5.0f;
    } // namespace

    void InterestGrid::Configure(float cellSize, float worldMin, float worldMax) {
        _cellSize = cellSize;
        _min      = worldMin;
        _max      = worldMax;
        _ready    = false; // re-initialised on next BeginRebuild()
    }

    void InterestGrid::SetBudget(uint32_t typeId, uint32_t maxCount) {
        if (maxCount == 0) {
            _budgets.erase(typeId);
            return;
        }
        _budgets[typeId] = maxCount;
    }

    uint32_t InterestGrid::BudgetFor(uint32_t typeId) const {
        const auto it = _budgets.find(typeId);
        return it != _budgets.end() ? it->second : 0;
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
        _maxEntityRange = 0.0f;
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
        _maxEntityRange = std::max(_maxEntityRange, entity->streaming.range);
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

    void InterestGrid::GatherCandidates(const glm::vec3 &center, float radius, std::unordered_set<NetworkEntity *> &out) {
        if (!_ready) {
            return;
        }
        const float centerV = GroundV(center);
        _queryHits.Clear(true, _FILE_AND_LINE_);
        _grid.GetEntries(_queryHits, center.x - radius, centerV - radius, center.x + radius, centerV + radius);

        for (unsigned i = 0; i < _queryHits.Size(); ++i) {
            auto *entity = static_cast<NetworkEntity *>(_queryHits[i]);
            // The grid can hold entries for entities destroyed since the last rebuild; only _live
            // pointers are safe to dereference.
            if (!entity || !_live.contains(entity)) {
                continue;
            }
            // Multi-cell entries and overlapping focus points repeat; the set dedupes.
            out.insert(entity);
        }
    }

    float InterestGrid::NearestFocusDistSq(const glm::vec3 &p) const {
        const float x = p.x;
        const float v = GroundV(p);
        float best    = std::numeric_limits<float>::max();
        for (const glm::vec3 &focus : _focus) {
            const float dx = x - focus.x;
            const float dv = v - GroundV(focus);
            best           = std::min(best, dx * dx + dv * dv);
        }
        return best;
    }

    const std::unordered_set<NetworkEntity *> *InterestGrid::OwnedBy(MafiaNet::PeerGuid guid) const {
        const auto it = _ownedByGuid.find(guid);
        return it != _ownedByGuid.end() ? &it->second : nullptr;
    }

    void InterestGrid::CollectVisible(NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, const std::unordered_set<NetworkEntity *> &previous, std::unordered_set<NetworkEntity *> &out) {
        if (!viewer) {
            return;
        }
        if (!_ready) {
            if (!_warnedNotReady) {
                _warnedNotReady = true;
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("InterestGrid queried before its first RebuildInterest(); no entities will replicate. Call ReplicationManager::RebuildInterest() once per server tick.");
            }
            return;
        }
        const auto observerWorld = viewer->GetVirtualWorld();

        // A private entity (targetGUID set) is only ever relevant to its target connection, and for
        // that viewer the target stands in for the dimension gate.
        const auto targetMatch = [viewerGUID](const NetworkEntity *entity) {
            return entity->streaming.targetGUID == MafiaNet::UNASSIGNED_PEER_GUID || entity->streaming.targetGUID == viewerGUID;
        };
        const auto targeted = [](const NetworkEntity *entity) {
            return entity->streaming.targetGUID != MafiaNet::UNASSIGNED_PEER_GUID;
        };

        // Focus points: the avatar, plus where its velocity puts it a moment from now. Relevance
        // takes the nearest, so the look-ahead only adds what is coming up.
        _focus.clear();
        _focus.push_back(viewer->position);
        if (_lookaheadSeconds > 0.0f) {
            const glm::vec3 ahead = viewer->position + viewer->velocity * _lookaheadSeconds;
            const float dx        = ahead.x - viewer->position.x;
            const float dv        = GroundV(ahead) - GroundV(viewer->position);
            if (dx * dx + dv * dv > kMinLookaheadDist * kMinLookaheadDist) {
                _focus.push_back(ahead);
            }
        }

        // One query per focus point at the widest reach any entity can claim; the exact per-entity
        // range test happens below.
        const float viewerRange = viewer->streaming.range;
        const float queryRadius = std::max(viewerRange, _maxEntityRange) + _streamOutMargin;
        _candidates.clear();
        for (const glm::vec3 &focus : _focus) {
            GatherCandidates(focus, queryRadius, _candidates);
        }

        // Owned and always-visible entities are skipped here and added unconditionally below: they
        // bypass range culling anyway, and counting them would let one player's cars evict others'.
        _ranked.clear();
        for (NetworkEntity *entity : _candidates) {
            if (entity == viewer || entity->ownerGUID == viewerGUID || entity->streaming.alwaysVisible) {
                continue;
            }
            if (!entity->streaming.visible || !targetMatch(entity)) {
                continue;
            }
            if (!targeted(entity) && !MafiaNet::VirtualWorldsCanSee(entity->GetVirtualWorld(), observerWorld)) {
                continue;
            }

            const bool wasRelevant = previous.contains(entity);
            // The entity's own range wins when longer; that is what makes per-type distances work.
            float range = std::max(viewerRange, entity->streaming.range);
            if (wasRelevant) {
                range += _streamOutMargin;
            }
            const float distSq = NearestFocusDistSq(entity->position);
            if (distSq > range * range) {
                continue;
            }

            const uint32_t budget = BudgetFor(entity->GetTypeId());
            if (budget == 0) {
                out.insert(entity);
                continue;
            }
            _ranked[entity->GetTypeId()].push_back({entity, wasRelevant ? distSq * kStickyDiscount : distSq});
        }

        // Budgeted types: keep the nearest `budget`, for this viewer only.
        for (auto &[typeId, candidates] : _ranked) {
            const uint32_t budget = BudgetFor(typeId);
            if (candidates.size() > budget) {
                const auto cut = candidates.begin() + static_cast<std::ptrdiff_t>(budget);
                std::nth_element(candidates.begin(), cut, candidates.end(), [](const Ranked &a, const Ranked &b) {
                    return a.rankDistSq < b.rankDistSq;
                });
                candidates.resize(budget);
            }
            for (const Ranked &candidate : candidates) {
                out.insert(candidate.entity);
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

        // A survivor drags in the entity it cannot be shown without, uncounted. Snapshotted first:
        // inserting into `out` while iterating it would invalidate the iteration.
        _dependencyScan.assign(out.begin(), out.end());
        for (NetworkEntity *entity : _dependencyScan) {
            NetworkEntity *dependency = entity->GetInterestDependency();
            if (!dependency || !_live.contains(dependency)) {
                continue;
            }
            if (dependency->streaming.visible && targetMatch(dependency)) {
                out.insert(dependency);
            }
        }
    }
} // namespace Framework::Networking::Replication
