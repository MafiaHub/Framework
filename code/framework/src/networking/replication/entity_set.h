/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace Framework::Networking::Replication {
    class NetworkEntity;

    // A set of entity pointers that keeps its storage across clear(). Interest refills one per viewer
    // on every refresh, where a node-based set allocates and frees an entry each time. Iterates in
    // insertion order until an erase, which moves the last entry into the gap.
    class EntitySet final {
      public:
        using const_iterator = std::vector<NetworkEntity *>::const_iterator;

        EntitySet() = default;
        EntitySet(std::initializer_list<NetworkEntity *> entities) {
            for (NetworkEntity *entity : entities) {
                insert(entity);
            }
        }

        bool insert(NetworkEntity *entity) {
            if ((_items.size() + 1) * 2 > _slots.size()) {
                Rehash(_slots.empty() ? 16 : _slots.size() * 2);
            }
            size_t slot = Home(entity);
            while (_slots[slot] != 0) {
                if (_items[_slots[slot] - 1] == entity) {
                    return false;
                }
                slot = (slot + 1) & (_slots.size() - 1);
            }
            _items.push_back(entity);
            _slots[slot] = static_cast<uint32_t>(_items.size());
            return true;
        }

        bool contains(const NetworkEntity *entity) const {
            return Find(entity) != kNone;
        }

        bool erase(const NetworkEntity *entity) {
            size_t slot = Find(entity);
            if (slot == kNone) {
                return false;
            }
            const size_t mask  = _slots.size() - 1;
            const uint32_t at  = _slots[slot] - 1;
            const uint32_t end = static_cast<uint32_t>(_items.size() - 1);
            if (at != end) {
                _slots[Find(_items[end])] = at + 1;
                _items[at]                = _items[end];
            }
            _items.pop_back();
            // Backward-shift deletion: pull each displaced follower into the hole unless its home lies
            // cyclically after the hole, so every probe chain stays unbroken without tombstones.
            for (size_t next = (slot + 1) & mask; _slots[next] != 0; next = (next + 1) & mask) {
                const size_t home = Home(_items[_slots[next] - 1]);
                if (((next - home) & mask) >= ((next - slot) & mask)) {
                    _slots[slot] = _slots[next];
                    slot         = next;
                }
            }
            _slots[slot] = 0;
            return true;
        }

        void clear() {
            if (_items.size() * 4 < _slots.size()) {
                // Sparse: empty just the occupied slots. Each entry still sits on its own probe path,
                // so the walk from its home finds it however many earlier slots are already empty.
                const size_t mask = _slots.size() - 1;
                for (size_t i = 0; i < _items.size(); ++i) {
                    size_t slot = Home(_items[i]);
                    while (_slots[slot] != i + 1) {
                        slot = (slot + 1) & mask;
                    }
                    _slots[slot] = 0;
                }
            }
            else {
                std::fill(_slots.begin(), _slots.end(), 0u);
            }
            _items.clear();
        }

        void swap(EntitySet &other) noexcept {
            _items.swap(other._items);
            _slots.swap(other._slots);
            std::swap(_shift, other._shift);
        }

        size_t size() const {
            return _items.size();
        }
        bool empty() const {
            return _items.empty();
        }
        NetworkEntity *operator[](size_t index) const {
            return _items[index];
        }
        const_iterator begin() const {
            return _items.begin();
        }
        const_iterator end() const {
            return _items.end();
        }

      private:
        static constexpr size_t kNone = ~size_t {0};

        // Fibonacci hashing: the multiply carries the pointer's varying middle bits into the top ones.
        size_t Home(const NetworkEntity *entity) const {
            return static_cast<size_t>((reinterpret_cast<uintptr_t>(entity) * UINT64_C(0x9E3779B97F4A7C15)) >> _shift);
        }

        size_t Find(const NetworkEntity *entity) const {
            if (_items.empty()) {
                return kNone;
            }
            for (size_t slot = Home(entity);; slot = (slot + 1) & (_slots.size() - 1)) {
                const uint32_t item = _slots[slot];
                if (item == 0) {
                    return kNone;
                }
                if (_items[item - 1] == entity) {
                    return slot;
                }
            }
        }

        void Rehash(size_t capacity) {
            _slots.assign(capacity, 0u);
            _shift = 64;
            for (size_t c = capacity; c > 1; c >>= 1) {
                --_shift;
            }
            for (size_t i = 0; i < _items.size(); ++i) {
                size_t slot = Home(_items[i]);
                while (_slots[slot] != 0) {
                    slot = (slot + 1) & (capacity - 1);
                }
                _slots[slot] = static_cast<uint32_t>(i + 1);
            }
        }

        std::vector<NetworkEntity *> _items;
        // 1-based indices into _items, 0 = empty; a power of two, at most half full.
        std::vector<uint32_t> _slots;
        unsigned _shift = 64;
    };
} // namespace Framework::Networking::Replication
