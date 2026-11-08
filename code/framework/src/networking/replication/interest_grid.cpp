/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "interest_grid.h"

#include <algorithm>
#include <cmath>

namespace Framework::Networking::Replication {
    namespace {
        // Under this the look-ahead point sits inside the avatar's own bubble, so the second grid
        // query would be pure cost.
        constexpr float kMinLookaheadDist = 5.0f;

        // An entity already in the set ranks at 72% of its true distance², so a challenger must be
        // ~15% nearer to evict it. Ranking only; never admits what the range check rejected.
        constexpr float kStickyDiscount = 0.72f;

        bool Targeted(const NetworkEntity *entity) {
            return entity->streaming.targetGUID != MafiaNet::UNASSIGNED_PEER_GUID;
        }

        void EraseValue(std::vector<uint32_t> &values, uint32_t value) {
            const auto it = std::find(values.begin(), values.end(), value);
            if (it != values.end()) {
                *it = values.back();
                values.pop_back();
            }
        }
    } // namespace

    uint32_t InterestGrid::Scratch::Begin(std::vector<uint32_t> &stamps, uint32_t &value, uint32_t capacity) {
        if (stamps.size() < capacity) {
            stamps.resize(capacity, 0);
        }
        if (++value == 0) {
            // Wrapped: a stale stamp could now read as current.
            std::fill(stamps.begin(), stamps.end(), 0);
            value = 1;
        }
        return value;
    }

    void InterestGrid::Configure(float cellSize, float worldMin, float worldMax) {
        _cellSize = cellSize;
        _min      = worldMin;
        _max      = worldMax;
        // Cell numbers change with the extent: everything is filed afresh on the next sync.
        for (uint32_t slot = 0; slot < _entries.size(); ++slot) {
            if (_entries[slot].entity != nullptr) {
                Unfile(slot);
            }
        }
        _grid         = {};
        _ownRangeGrid = {};
    }

    void InterestGrid::SetGroundPlaneXY(bool groundXY) {
        if (_groundXY == groundXY) {
            return;
        }
        _groundXY = groundXY;
        Configure(_cellSize, _min, _max);
    }

    void InterestGrid::SetBudget(uint32_t typeId, uint32_t maxCount) {
        if (maxCount == 0) {
            _budgets.erase(typeId);
            return;
        }
        _budgets[typeId] = maxCount;
    }

    uint32_t InterestGrid::BudgetFor(uint32_t typeId) const {
        if (_budgets.empty()) {
            return 0;
        }
        const auto it = _budgets.find(typeId);
        return it != _budgets.end() ? it->second : 0;
    }

    void InterestGrid::SetUsesEntityRange(uint32_t typeId, bool usesEntityRange) {
        if (usesEntityRange) {
            _entityRangeTypes.insert(typeId);
        }
        else {
            _entityRangeTypes.erase(typeId);
        }
    }

    float InterestGrid::RangeFor(const NetworkEntity *entity, float viewerRange) const {
        // The entity's own range wins when longer; that is what makes per-type distances work.
        // A type that opted in is held to its own range even when the viewer's is longer.
        if (!_entityRangeTypes.empty() && _entityRangeTypes.contains(entity->GetTypeId())) {
            return entity->streaming.range;
        }
        return std::max(viewerRange, entity->streaming.range);
    }

    bool InterestGrid::InDimension(const NetworkEntity *entity, MafiaNet::VirtualWorldId observerWorld) const {
        // A private entity (targetGUID set) is only ever relevant to its target connection, and for
        // that viewer the target stands in for the dimension gate.
        return Targeted(entity) || MafiaNet::VirtualWorldsCanSee(entity->GetVirtualWorld(), observerWorld);
    }

    void InterestGrid::EnsureGrid(Grid &grid) {
        if (grid.ready) {
            return;
        }
        const float span = std::max(_max - _min, _cellSize);
        grid.width       = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(span / _cellSize)));
        grid.cells.assign(static_cast<size_t>(grid.width) * grid.width, {});
        grid.ready = true;
    }

    uint32_t InterestGrid::CellOf(const glm::vec3 &position) const {
        const uint32_t width = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(std::max(_max - _min, _cellSize) / _cellSize)));
        const auto axis      = [&](float value) {
            const float cell = std::floor((value - _min) / _cellSize);
            if (!(cell > 0.0f)) {
                return 0u; // also catches NaN
            }
            return std::min(static_cast<uint32_t>(cell), width - 1);
        };
        return axis(GroundV(position)) * width + axis(position.x);
    }

    void InterestGrid::File(uint32_t slot, bool ownRange, uint32_t cell) {
        Grid &grid = ownRange ? _ownRangeGrid : _grid;
        EnsureGrid(grid);
        std::vector<uint32_t> &bucket = grid.cells[cell];
        Entry &entry                  = _entries[slot];
        entry.cell                    = cell;
        entry.indexInCell             = static_cast<uint32_t>(bucket.size());
        entry.ownRange                = ownRange;
        bucket.push_back(slot);
        if (ownRange) {
            ++_ownRangeCount;
        }
    }

    void InterestGrid::Unfile(uint32_t slot) {
        Entry &entry = _entries[slot];
        if (entry.cell == kNoCell) {
            return;
        }
        Grid &grid                    = entry.ownRange ? _ownRangeGrid : _grid;
        std::vector<uint32_t> &bucket = grid.cells[entry.cell];
        const uint32_t moved          = bucket.back();
        bucket[entry.indexInCell]     = moved;
        _entries[moved].indexInCell   = entry.indexInCell;
        bucket.pop_back();
        entry.cell = kNoCell;
        if (entry.ownRange) {
            --_ownRangeCount;
        }
    }

    void InterestGrid::OwnerAdd(uint32_t slot, MafiaNet::PeerGuid owner) {
        if (owner != MafiaNet::UNASSIGNED_PEER_GUID) {
            _ownedByGuid[owner].push_back(slot);
        }
    }

    void InterestGrid::OwnerRemove(uint32_t slot, MafiaNet::PeerGuid owner) {
        if (owner == MafiaNet::UNASSIGNED_PEER_GUID) {
            return;
        }
        const auto bucket = _ownedByGuid.find(owner);
        if (bucket == _ownedByGuid.end()) {
            return;
        }
        EraseValue(bucket->second, slot);
        if (bucket->second.empty()) {
            _ownedByGuid.erase(bucket);
        }
    }

    void InterestGrid::BeginSync() {
        if (++_syncStamp == 0) {
            _syncStamp = 1;
        }
        _maxEntityRange = 0.0f;
        _maxOwnRange    = 0.0f;
        _alwaysVisible.clear();
        _dropped.clear();
        _dependencyChanges.clear();
    }

    void InterestGrid::Sync(NetworkEntity *entity) {
        if (!entity) {
            return;
        }
        uint32_t slot = entity->_interestSlot;
        if (slot == kNoSlot) {
            if (_freeSlots.empty()) {
                slot = static_cast<uint32_t>(_entries.size());
                _entries.emplace_back();
            }
            else {
                slot = _freeSlots.back();
                _freeSlots.pop_back();
            }
            _entries[slot].entity = entity;
            entity->_interestSlot = slot;
        }

        Entry &entry        = _entries[slot];
        entry.syncStamp     = _syncStamp;
        const bool own      = !_entityRangeTypes.empty() && _entityRangeTypes.contains(entity->GetTypeId());
        const uint32_t cell = CellOf(entity->position);
        if (entry.cell != cell || entry.ownRange != own) {
            Unfile(slot);
            File(slot, own, cell);
        }
        if (own) {
            _maxOwnRange = std::max(_maxOwnRange, entity->streaming.range);
        }
        else {
            _maxEntityRange = std::max(_maxEntityRange, entity->streaming.range);
        }

        if (entry.owner != entity->ownerGUID) {
            OwnerRemove(slot, entry.owner);
            OwnerAdd(slot, entity->ownerGUID);
            entry.owner = entity->ownerGUID;
        }
        if (entity->streaming.alwaysVisible) {
            _alwaysVisible.push_back(slot);
        }
    }

    uint32_t InterestGrid::DependencyOf(uint32_t slot) const {
        if (slot >= _entries.size()) {
            return kNoSlot;
        }
        // The dependency may have left since the sync that resolved it, and its slot been reused.
        const Entry &entry = _entries[slot];
        if (entry.dependency == kNoSlot || _entries[entry.dependency].generation != entry.dependencyGeneration || _entries[entry.dependency].entity == nullptr) {
            return kNoSlot;
        }
        return entry.dependency;
    }

    const std::vector<uint32_t> &InterestGrid::EndSync() {
        for (uint32_t slot = 0; slot < _entries.size(); ++slot) {
            Entry &entry = _entries[slot];
            if (entry.entity != nullptr && entry.syncStamp != _syncStamp) {
                // Gone without a Remove: the pointer may already be freed, so it is not touched.
                Release(slot);
                _dropped.push_back(slot);
            }
        }
        // After the releases, so a dependency that left this pass resolves to nothing.
        for (uint32_t slot = 0; slot < _entries.size(); ++slot) {
            Entry &entry = _entries[slot];
            if (entry.entity == nullptr) {
                continue;
            }
            const uint32_t before           = DependencyOf(slot);
            const NetworkEntity *dependency = entry.entity->GetInterestDependency();
            entry.dependency                = dependency ? dependency->_interestSlot : kNoSlot;
            entry.dependencyGeneration      = entry.dependency != kNoSlot ? _entries[entry.dependency].generation : 0;
            const uint32_t after            = DependencyOf(slot);
            if (after != kNoSlot && after != before) {
                _dependencyChanges.push_back(slot);
            }
        }
        return _dropped;
    }

    void InterestGrid::Remove(NetworkEntity *entity) {
        if (!entity || entity->_interestSlot == kNoSlot) {
            return;
        }
        const uint32_t slot   = entity->_interestSlot;
        entity->_interestSlot = kNoSlot;
        Release(slot);
    }

    void InterestGrid::Release(uint32_t slot) {
        Unfile(slot);
        Entry &entry = _entries[slot];
        OwnerRemove(slot, entry.owner);
        EraseValue(_alwaysVisible, slot);
        const uint32_t generation = entry.generation + 1;
        entry                     = {};
        entry.generation          = generation;
        _freeSlots.push_back(slot);
    }

    void InterestGrid::Reown(NetworkEntity *entity, MafiaNet::PeerGuid previousOwner) {
        (void)previousOwner; // the index knows who it filed the entity under
        if (!entity || entity->_interestSlot == kNoSlot) {
            return;
        }
        const uint32_t slot = entity->_interestSlot;
        Entry &entry        = _entries[slot];
        if (entry.owner == entity->ownerGUID) {
            return;
        }
        OwnerRemove(slot, entry.owner);
        OwnerAdd(slot, entity->ownerGUID);
        entry.owner = entity->ownerGUID;
    }

    InterestGrid::Focus InterestGrid::FocusOf(const NetworkEntity *viewer) const {
        // Focus points: the avatar, plus where its velocity puts it a moment from now. Relevance
        // takes the nearest, so the look-ahead only adds what is coming up.
        Focus focus;
        focus.points[focus.count++] = viewer->position;
        if (_lookaheadSeconds > 0.0f) {
            const glm::vec3 ahead = viewer->position + viewer->velocity * _lookaheadSeconds;
            const float dx        = ahead.x - viewer->position.x;
            const float dv        = GroundV(ahead) - GroundV(viewer->position);
            if (dx * dx + dv * dv > kMinLookaheadDist * kMinLookaheadDist) {
                focus.points[focus.count++] = ahead;
            }
        }
        return focus;
    }

    float InterestGrid::NearestFocusDistSq(const Focus &focus, const glm::vec3 &p) const {
        const float v = GroundV(p);
        float best    = std::numeric_limits<float>::max();
        for (uint32_t i = 0; i < focus.count; ++i) {
            const float dx = p.x - focus.points[i].x;
            const float dv = v - GroundV(focus.points[i]);
            best           = std::min(best, dx * dx + dv * dv);
        }
        return best;
    }

    void InterestGrid::Gather(const Grid &grid, const glm::vec3 &center, float radius, Scratch &scratch) const {
        if (!grid.ready) {
            return;
        }
        const auto axis = [&](float value) {
            const float cell = std::floor((value - _min) / _cellSize);
            if (!(cell > 0.0f)) {
                return 0u;
            }
            return std::min(static_cast<uint32_t>(cell), grid.width - 1);
        };
        const float v        = GroundV(center);
        const uint32_t x0    = axis(center.x - radius);
        const uint32_t x1    = axis(center.x + radius);
        const uint32_t y0    = axis(v - radius);
        const uint32_t y1    = axis(v + radius);
        const uint32_t stamp = scratch.candidateStamp;
        for (uint32_t y = y0; y <= y1; ++y) {
            const std::vector<uint32_t> *row = &grid.cells[static_cast<size_t>(y) * grid.width];
            for (uint32_t x = x0; x <= x1; ++x) {
                for (const uint32_t slot : row[x]) {
                    if (scratch.candidateStamps[slot] != stamp) {
                        scratch.candidateStamps[slot] = stamp;
                        scratch.candidates.push_back(slot);
                    }
                }
            }
        }
    }

    void InterestGrid::CollectVisible(const NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, const SlotSet &previous, Scratch &scratch, std::vector<uint32_t> &out) const {
        out.clear();
        if (!viewer) {
            return;
        }
        const uint32_t capacity = Capacity();
        const auto observer     = viewer->GetVirtualWorld();
        const Focus focus       = FocusOf(viewer);
        const float viewerRange = viewer->streaming.range;

        // One query per focus point at the widest reach any entity can claim; the exact per-entity
        // range test happens below.
        scratch.candidates.clear();
        scratch.Begin(scratch.candidateStamps, scratch.candidateStamp, capacity);
        const float queryRadius = std::max(viewerRange, _maxEntityRange) + _streamOutMargin;
        for (uint32_t i = 0; i < focus.count; ++i) {
            Gather(_grid, focus.points[i], queryRadius, scratch);
        }
        if (_ownRangeCount != 0) {
            const float ownRangeRadius = _maxOwnRange + _streamOutMargin;
            for (uint32_t i = 0; i < focus.count; ++i) {
                Gather(_ownRangeGrid, focus.points[i], ownRangeRadius, scratch);
            }
        }

        const uint32_t outStamp = scratch.Begin(scratch.outStamps, scratch.outStamp, capacity);
        const auto add          = [&](uint32_t slot) {
            if (scratch.outStamps[slot] != outStamp) {
                scratch.outStamps[slot] = outStamp;
                out.push_back(slot);
            }
        };

        // Owned and always-visible entities are skipped here and added unconditionally below: they
        // bypass range culling anyway, and counting them would let one player's cars evict others'.
        scratch.ranked.clear();
        for (const uint32_t slot : scratch.candidates) {
            const NetworkEntity *entity = _entries[slot].entity;
            if (entity == viewer || entity->ownerGUID == viewerGUID || entity->streaming.alwaysVisible) {
                continue;
            }
            if (!entity->streaming.visible || !entity->streaming.Admits(viewerGUID) || !InDimension(entity, observer)) {
                continue;
            }
            const bool wasRelevant = previous.Test(slot);
            float range            = RangeFor(entity, viewerRange);
            if (wasRelevant) {
                range += _streamOutMargin;
            }
            const float distSq = NearestFocusDistSq(focus, entity->position);
            if (distSq > range * range) {
                continue;
            }
            if (BudgetFor(entity->GetTypeId()) == 0) {
                add(slot);
                continue;
            }
            scratch.ranked.push_back({entity->GetTypeId(), slot, wasRelevant ? distSq * kStickyDiscount : distSq});
        }

        // Budgeted types: keep the nearest `budget`, for this viewer only.
        if (!scratch.ranked.empty()) {
            std::sort(scratch.ranked.begin(), scratch.ranked.end(), [](const Scratch::Ranked &a, const Scratch::Ranked &b) {
                return a.typeId != b.typeId ? a.typeId < b.typeId : a.rankDistSq < b.rankDistSq;
            });
            uint32_t typeId = scratch.ranked.front().typeId;
            uint32_t budget = BudgetFor(typeId);
            uint32_t taken  = 0;
            for (const Scratch::Ranked &candidate : scratch.ranked) {
                if (candidate.typeId != typeId) {
                    typeId = candidate.typeId;
                    budget = BudgetFor(typeId);
                    taken  = 0;
                }
                if (taken < budget) {
                    add(candidate.slot);
                    ++taken;
                }
            }
        }

        if (const auto owned = _ownedByGuid.find(viewerGUID); owned != _ownedByGuid.end()) {
            for (const uint32_t slot : owned->second) {
                const NetworkEntity *entity = _entries[slot].entity;
                if (entity->streaming.visible && entity->streaming.Admits(viewerGUID)) {
                    add(slot);
                }
            }
        }
        for (const uint32_t slot : _alwaysVisible) {
            const NetworkEntity *entity = _entries[slot].entity;
            if (entity->streaming.visible && entity->streaming.Admits(viewerGUID) && InDimension(entity, observer)) {
                add(slot);
            }
        }
        const uint32_t viewerSlot = SlotOf(viewer);
        if (viewerSlot != kNoSlot && viewer->streaming.visible && viewer->streaming.Admits(viewerGUID)) {
            add(viewerSlot);
        }

        // A survivor drags in the entity it cannot be shown without, uncounted. Followed one level:
        // only what was in the set before this loop is scanned.
        const size_t survivors = out.size();
        for (size_t i = 0; i < survivors; ++i) {
            const uint32_t dependency = DependencyOf(out[i]);
            if (dependency == kNoSlot) {
                continue;
            }
            const NetworkEntity *entity = _entries[dependency].entity;
            if (entity->streaming.visible && entity->streaming.Admits(viewerGUID)) {
                add(dependency);
            }
        }
    }

    InterestGrid::Verdict InterestGrid::Evaluate(const NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, uint32_t slot, bool wasRelevant) const {
        const NetworkEntity *entity = EntityAt(slot);
        if (!viewer || !entity || !entity->streaming.visible || !entity->streaming.Admits(viewerGUID)) {
            return Verdict::Out;
        }
        if (entity == viewer || entity->ownerGUID == viewerGUID) {
            return Verdict::In;
        }
        const auto observer = viewer->GetVirtualWorld();
        if (!InDimension(entity, observer)) {
            return Verdict::Out;
        }
        if (entity->streaming.alwaysVisible) {
            return Verdict::In;
        }
        float range = RangeFor(entity, viewer->streaming.range);
        if (wasRelevant) {
            range += _streamOutMargin;
        }
        const bool inRange = NearestFocusDistSq(FocusOf(viewer), entity->position) <= range * range;
        if (BudgetFor(entity->GetTypeId()) != 0 && inRange != wasRelevant) {
            return Verdict::Recompute;
        }
        return inRange ? Verdict::In : Verdict::Out;
    }
} // namespace Framework::Networking::Replication
