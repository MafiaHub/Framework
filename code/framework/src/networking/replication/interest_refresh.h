/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>

namespace Framework::Networking::Replication {
    // Each viewer refreshes once per interval, aligned to a stable phase rather
    // than to the shared grid rebuild. Missed periods are coalesced after stalls.
    class InterestRefresh final {
      public:
        bool Due(uint64_t now, uint32_t interval, uint64_t viewerKey, bool urgent, bool gridChanged) {
            if (interval == 0) {
                _interval = 0;
                return urgent || gridChanged;
            }
            if (!urgent && interval == _interval && now >= _last && now < _next) {
                return false;
            }
            // Mix all GUID bits; connection identifiers often share low bits.
            uint64_t hash = viewerKey;
            hash          = (hash ^ (hash >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
            hash          = (hash ^ (hash >> 27)) * UINT64_C(0x94d049bb133111eb);
            hash ^= hash >> 31;
            const uint64_t phase = hash % interval;
            const uint64_t delay = (phase + interval - now % interval) % interval;
            _next                = now + (delay == 0 ? interval : delay);
            _last                = now;
            _interval            = interval;
            return true;
        }

      private:
        uint64_t _next     = 0;
        uint64_t _last     = 0;
        uint32_t _interval = 0;
    };
} // namespace Framework::Networking::Replication
