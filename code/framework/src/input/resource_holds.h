/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <map>
#include <string>

namespace Framework::Input {
    // Counts only the named owner's holds. The game applies each transition
    // and releases TakeAll(owner) holds when that resource stops.
    class ResourceHolds final {
      public:
        void Acquire(const std::string &owner) {
            ++_holds[owner];
        }
        bool Release(const std::string &owner) {
            const auto held = _holds.find(owner);
            if (held == _holds.end()) {
                return false;
            }
            if (--held->second == 0) {
                _holds.erase(held);
            }
            return true;
        }
        int TakeAll(const std::string &owner) {
            const auto held = _holds.find(owner);
            if (held == _holds.end()) {
                return 0;
            }
            const int count = held->second;
            _holds.erase(held);
            return count;
        }
        // Idempotent ownership for setEnabled-style APIs.
        void Set(const std::string &owner, bool held) {
            if (held && !IsHeld(owner)) {
                Acquire(owner);
            }
            else if (!held) {
                TakeAll(owner);
            }
        }
        bool IsHeld(const std::string &owner) const {
            return _holds.find(owner) != _holds.end();
        }
        bool IsHeld() const {
            return !_holds.empty();
        }
        void Clear() {
            _holds.clear();
        }

      private:
        std::map<std::string, int> _holds;
    };
} // namespace Framework::Input
