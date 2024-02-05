/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <vector>

namespace Framework::Utils {
    template <typename T>
    class SlotManager {
      private:
        std::vector<bool> _occupied;
        std::vector<T> _slots;
        std::vector<size_t> _freeList;
        bool _recycle = false;

      public:
        SlotManager() {
            _occupied.reserve(64);
            _slots.reserve(64);
            _freeList.reserve(64);
        }

        void RecycleSlots(bool recycle) {
            _recycle = recycle;
        }

        size_t AllocateSlot(const T &val) {
            if (_freeList.empty() || !_recycle) {
                _occupied.push_back(true);
                _slots.push_back(val);
                return _slots.size() - 1;
            }
            else {
                size_t id = _freeList.back();
                _freeList.pop_back();
                _slots[id]    = val;
                _occupied[id] = true;
                return id;
            }
        }

        void DeallocateSlot(size_t id) {
            if (id < _occupied.size()) {
                _occupied[id] = false;

                if (_recycle)
                    _freeList.push_back(id);
            }
        }

        void DeallocateSlot(const T &val) {
            for (size_t i = 0; i < _slots.size(); i++) {
                if (_slots[i] == val) {
                    DeallocateSlot(i);
                    return;
                }
            }
        }

        T *GetSlot(size_t id) {
            if (id < _slots.size() && _occupied[id]) {
                return &_slots[id];
            }
            return nullptr;
        }

        const T *GetSlot(size_t id) const {
            if (id < _slots.size() && _occupied[id]) {
                return &_slots[id];
            }
            return nullptr;
        }
    };
} // namespace Framework::Utils
