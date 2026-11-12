/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "network_entity.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Framework::Networking::Replication {
    // Membership over interest slots: one bit per slot, so a viewer's set costs a few hundred bytes
    // however it is queried, and testing it never hashes.
    class SlotSet final {
      public:
        bool Test(uint32_t slot) const {
            const uint32_t word = slot >> 6;
            return word < _words.size() && (_words[word] >> (slot & 63)) & 1u;
        }
        void Set(uint32_t slot) {
            const uint32_t word = slot >> 6;
            if (word >= _words.size()) {
                _words.resize(word + 1, 0);
            }
            _words[word] |= uint64_t {1} << (slot & 63);
        }
        void Reset(uint32_t slot) {
            const uint32_t word = slot >> 6;
            if (word < _words.size()) {
                _words[word] &= ~(uint64_t {1} << (slot & 63));
            }
        }
        // Keeps the storage: a set that is refilled every refresh allocates once.
        void Clear() {
            std::fill(_words.begin(), _words.end(), 0);
        }
        // Grows to hold `slots` without allocating later, as long as the slot count stays below it.
        void Reserve(uint32_t slots) {
            const size_t words = (static_cast<size_t>(slots) + 63) / 64;
            if (_words.size() < words) {
                _words.resize(words, 0);
            }
        }
        void Swap(SlotSet &other) {
            _words.swap(other._words);
        }
        size_t Count() const {
            size_t count = 0;
            for (const uint64_t word : _words) {
                count += static_cast<size_t>(std::popcount(word));
            }
            return count;
        }
        template <typename Fn>
        void ForEach(Fn &&fn) const {
            for (size_t word = 0; word < _words.size(); ++word) {
                for (uint64_t bits = _words[word]; bits != 0; bits &= bits - 1) {
                    fn(static_cast<uint32_t>(word * 64 + std::countr_zero(bits)));
                }
            }
        }

      private:
        std::vector<uint64_t> _words;
    };

    // The server's spatial interest index and the relevance rule over it.
    //
    // Every replicated entity holds a slot: a small dense index, stable while it lives, that viewers'
    // sets, scratch and per-entity bookkeeping are indexed by instead of hashed. The index is kept,
    // not rebuilt: Sync re-files an entity only when it crosses a cell, so a tick costs one pass over
    // the entities and nothing for the ones that stayed put. A destroyed entity leaves the index at
    // once (Remove), so no query can hand out its pointer.
    //
    // Queries are const and write only to the Scratch they are given: several threads may run them
    // at once, each with its own, while nothing changes the index.
    class InterestGrid final {
      public:
        static constexpr uint32_t kNoSlot = std::numeric_limits<uint32_t>::max();

        // Per-thread working memory for queries. Reused across them, so a settled server allocates
        // nothing per query.
        class Scratch final {
          public:
            Scratch() = default;

          private:
            friend class InterestGrid;
            struct Ranked {
                uint32_t typeId;
                uint32_t slot;
                float rankDistSq;
            };
            uint32_t Begin(std::vector<uint32_t> &stamps, uint32_t &value, uint32_t capacity);

            std::vector<uint32_t> candidates;
            std::vector<uint32_t> candidateStamps;
            uint32_t candidateStamp = 0;
            std::vector<uint32_t> outStamps;
            uint32_t outStamp = 0;
            std::vector<Ranked> ranked;
        };

        // Spatial index extent. Pick bounds that enclose the playable area; entities outside clamp
        // to edge cells (still found by radius queries, just less precisely). Re-files everything.
        void Configure(float cellSize, float worldMin, float worldMax);

        // Select the horizontal plane for relevance: false (default) uses XZ (Y-up engines); true uses
        // XY (Z-up engines, e.g. Mafia 2). Streaming distance must be measured on the ground plane, so
        // a Z-up game whose height is Z has to set this or height bleeds into the radius check.
        void SetGroundPlaneXY(bool groundXY);

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

        // By default an entity streams at the larger of its own range and the viewer's, so its
        // range can only widen interest. For a type set here the entity's own streaming.range is
        // the whole radius and the viewer's is ignored, which lets dense scenery stream nearer
        // than the viewer sees everything else. The stream-out margin still applies.
        //
        // Such entities are filed in a grid of their own, queried only as far as the longest of
        // their ranges, so however many there are they never enter a viewer's wide query.
        void SetUsesEntityRange(uint32_t typeId, bool usesEntityRange);

        // --- The index (one thread, never during a query) ---

        // A tick's pass: BeginSync, Sync for every live entity, EndSync. An entity seen for the first
        // time gets a slot; one not seen since the last pass leaves (Remove covers deletion, this
        // catches anything that stopped being replicated without it).
        void BeginSync();
        void Sync(NetworkEntity *entity);
        // Hands back the slots that left without a Remove, for the caller to forget. Dependencies are
        // resolved here, once every entity of the pass has a slot, whatever order they were synced in.
        const std::vector<uint32_t> &EndSync();
        // Slots whose dependency became another live entity at the last EndSync: a viewer already
        // showing one has to be handed what it now depends on.
        const std::vector<uint32_t> &DependencyChanges() const {
            return _dependencyChanges;
        }

        // Drop an entity now. Its slot is free for the next entity once this returns.
        void Remove(NetworkEntity *entity);

        // Re-file an entity whose ownerGUID just changed, so the owner's next query finds it.
        //
        // Not optional bookkeeping. CollectVisible skips an entity owned by the viewer from range and
        // budget ranking and re-adds it from the owned index instead, so the two have to agree: an
        // entity whose ownerGUID names the viewer but which is missing from that viewer's bucket is in
        // neither set and drops out of the relevant set entirely -- ReplicaManager3 then destroys the
        // replica on the one peer that was just told to simulate it.
        void Reown(NetworkEntity *entity, MafiaNet::PeerGuid previousOwner);

        uint32_t SlotOf(const NetworkEntity *entity) const {
            return entity ? entity->_interestSlot : kNoSlot;
        }
        // Null for a free slot.
        NetworkEntity *EntityAt(uint32_t slot) const {
            return slot < _entries.size() ? _entries[slot].entity : nullptr;
        }
        // Bumped each time the slot is freed, so per-slot state left by a previous occupant can be
        // told from the current one's.
        uint32_t GenerationOf(uint32_t slot) const {
            return slot < _entries.size() ? _entries[slot].generation : 0;
        }
        // One past the highest slot ever handed out: size per-slot arrays to this.
        uint32_t Capacity() const {
            return static_cast<uint32_t>(_entries.size());
        }

        // --- Queries ---

        // The entities relevant to `viewer`, as slots, in no particular order: in range and
        // dimension, owned, or always-visible, plus the dependency each survivor needs. The home of
        // the server's relevance rule.
        //
        // `previous` is the viewer's set from its last computation and drives both hysteresis rules
        // (stream-out margin, sticky ranking).
        void CollectVisible(const NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, const SlotSet &previous, Scratch &scratch, std::vector<uint32_t> &out) const;

        // One entity against one viewer, by the same rule. A budgeted type cannot be decided alone
        // (it ranks against the rest), so it answers Recompute when the outcome may have changed.
        enum class Verdict : uint8_t {
            Out,
            In,
            Recompute
        };
        Verdict Evaluate(const NetworkEntity *viewer, MafiaNet::PeerGuid viewerGUID, uint32_t slot, bool wasRelevant) const;

        // The entity a slot cannot be shown without (NetworkEntity::GetInterestDependency, resolved
        // at the last sync), or kNoSlot.
        uint32_t DependencyOf(uint32_t slot) const;

      private:
        static constexpr uint32_t kNoCell = std::numeric_limits<uint32_t>::max();

        struct Entry {
            NetworkEntity *entity         = nullptr;
            uint32_t generation           = 0;
            uint32_t cell                 = kNoCell;
            uint32_t indexInCell          = 0;
            uint32_t syncStamp            = 0;
            uint64_t id                   = 0; // network id, as of the last sync
            uint64_t dependencyId         = 0; // GetInterestDependency, as of the last sync
            uint32_t dependency           = kNoSlot;
            uint32_t dependencyGeneration = 0; // the dependency slot's generation when resolved
            MafiaNet::PeerGuid owner      = MafiaNet::UNASSIGNED_PEER_GUID;
            bool ownRange                 = false;
        };

        // A bounded grid of cells, each the slots filed in it. Cells keep their storage when emptied.
        struct Grid {
            std::vector<std::vector<uint32_t>> cells;
            uint32_t width = 0;
            bool ready     = false;
        };

        struct Focus {
            glm::vec3 points[2];
            uint32_t count = 0;
        };

        float GroundV(const glm::vec3 &p) const {
            return _groundXY ? p.y : p.z;
        }
        Focus FocusOf(const NetworkEntity *viewer) const;
        float NearestFocusDistSq(const Focus &focus, const glm::vec3 &p) const;
        uint32_t BudgetFor(uint32_t typeId) const;
        float RangeFor(const NetworkEntity *entity, float viewerRange) const;
        bool InDimension(const NetworkEntity *entity, MafiaNet::VirtualWorldId observerWorld) const;

        void EnsureGrid(Grid &grid);
        uint32_t CellOf(const glm::vec3 &position) const;
        void File(uint32_t slot, bool ownRange, uint32_t cell);
        void Unfile(uint32_t slot);
        void OwnerAdd(uint32_t slot, MafiaNet::PeerGuid owner);
        void OwnerRemove(uint32_t slot, MafiaNet::PeerGuid owner);
        void Release(uint32_t slot);
        void Gather(const Grid &grid, const glm::vec3 &center, float radius, Scratch &scratch) const;

        bool _groundXY          = false;
        float _cellSize         = 100.0f;
        float _min              = -10000.0f;
        float _max              = 10000.0f;
        float _streamOutMargin  = 0.0f;
        float _lookaheadSeconds = 0.0f;

        Grid _grid;
        // Entities of the types that use their own range; see SetUsesEntityRange.
        Grid _ownRangeGrid;
        // Widest per-entity range in each grid as of the last sync; a query must reach that far.
        float _maxEntityRange = 0.0f;
        float _maxOwnRange    = 0.0f;
        size_t _ownRangeCount = 0;

        std::vector<Entry> _entries;
        std::vector<uint32_t> _freeSlots;
        // Filed entities by network id: how a dependency, named by id, finds its slot.
        std::unordered_map<uint64_t, uint32_t> _slotById;
        uint32_t _syncStamp = 0;
        std::vector<uint32_t> _dropped;
        std::vector<uint32_t> _dependencyChanges;

        std::unordered_map<uint32_t, uint32_t> _budgets;
        std::unordered_set<uint32_t> _entityRangeTypes;
        std::unordered_map<MafiaNet::PeerGuid, std::vector<uint32_t>> _ownedByGuid;
        std::vector<uint32_t> _alwaysVisible;
    };
} // namespace Framework::Networking::Replication
