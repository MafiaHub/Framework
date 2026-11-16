/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace Framework::Networking {
    // The number a joined player is known by in scripts, logs and admin tools: a namespace of its
    // own, apart from entity network IDs. Those come from the replication manager's counter, which
    // every replicated entity draws from, so a server that spawns thousands of entities at startup
    // hands its first player an entity ID in the thousands. Player IDs see only joins.
    //
    // Allocated the way FiveM allocates server IDs: the first player to join after startup is 1, and
    // each join takes the next number. A disconnected player's ID is not handed out again, so an ID
    // in a log or a ban note names one session; a player who reconnects gets a new one. Only once
    // the counter passes maxId does it start over at 1, and then it skips every ID still held.
    //
    // Keyed by the connection's RakNetGUID value (RakNetGUID::g). Not synchronized: like the rest of
    // NetworkServer's per-connection state, it is used from the thread that processes packets.
    class PlayerIdAllocator final {
      public:
        using PlayerId = uint32_t;

        // No player: what a lookup returns for a connection that holds no ID.
        static constexpr PlayerId kInvalid = 0;

        explicit PlayerIdAllocator(PlayerId maxId = std::numeric_limits<PlayerId>::max()): _maxId(maxId) {}

        // The connection's ID, taking the next free one on its first call. kInvalid only when every
        // ID in 1..maxId is held at once.
        PlayerId Assign(uint64_t guid) {
            if (const auto it = _byGuid.find(guid); it != _byGuid.end()) {
                return it->second;
            }
            if (_byId.size() >= _maxId) {
                return kInvalid;
            }
            // Only true after a wrap: before it, every ID ahead of the counter is unused.
            while (_byId.contains(_next)) {
                Advance();
            }
            const PlayerId id = _next;
            Advance();
            _byGuid.emplace(guid, id);
            _byId.emplace(id, guid);
            return id;
        }

        // The connection is gone. Its ID is not reused until the counter wraps.
        void Release(uint64_t guid) {
            const auto it = _byGuid.find(guid);
            if (it == _byGuid.end()) {
                return;
            }
            _byId.erase(it->second);
            _byGuid.erase(it);
        }

        PlayerId Get(uint64_t guid) const {
            const auto it = _byGuid.find(guid);
            return it != _byGuid.end() ? it->second : kInvalid;
        }

        std::optional<uint64_t> FindGuid(PlayerId id) const {
            const auto it = _byId.find(id);
            if (it == _byId.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        // Held IDs, in no particular order.
        template <typename Fn>
        void ForEach(Fn &&fn) const {
            for (const auto &[id, guid] : _byId) {
                fn(id, guid);
            }
        }

        size_t Size() const {
            return _byId.size();
        }

      private:
        void Advance() {
            _next = _next >= _maxId ? 1 : _next + 1;
        }

        PlayerId _maxId;
        PlayerId _next = 1;
        std::unordered_map<uint64_t, PlayerId> _byGuid;
        std::unordered_map<PlayerId, uint64_t> _byId;
    };
} // namespace Framework::Networking
