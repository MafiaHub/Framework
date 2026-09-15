/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <mafianet/BitStream.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Framework::Networking::Replication {
    class NetworkEntity;

    // One value in a state bag. Scalars stay native so C++ reads them without parsing; the scripting
    // layer serializes anything structured to JSON and stores it as Json, so the wire format never
    // needs to know a game's shapes.
    struct StateValue {
        enum class Type : uint8_t {
            Null = 0,
            Boolean,
            Number,
            String,
            Json,
        };

        Type type     = Type::Null;
        bool boolean  = false;
        double number = 0.0;
        std::string text; // String payload, or the JSON document when type is Json.

        bool operator==(const StateValue &) const = default;

        void Serialize(MafiaNet::BitStream *bs, bool write);

        // Only the text varies; the tag and scalars are fixed width.
        size_t ByteSize() const {
            return text.size();
        }
    };

    // Who a key reaches.
    enum class StateScope : uint8_t {
        Broadcast = 0, // every connection the entity is constructed for (the default)
        Owner,         // only the entity's owning connection
        Server,        // never leaves the server; script-side storage at zero wire cost
    };

    // Which changes a subscription wants. A zero networkId matches any entity, an empty key any key;
    // both set is the narrow case a listener watching one field of one entity should be paying for.
    struct StateChangeFilter {
        uint64_t networkId = 0;
        std::string key;
    };

    struct StateChange {
        NetworkEntity *entity = nullptr;
        std::string key;
        StateValue value;
        StateValue previous;
        // Null is a value a script may store, so "was null" and "was not set" cannot be told apart
        // from `previous` alone.
        bool hadPrevious = false;
        bool removed     = false;
    };

    // An entity's arbitrary key/value state, replicated to the connections that can see the entity.
    //
    // This does not ride SerializeFields. That path runs through VariableDeltaSerializer, which
    // identifies variables by their position in a fixed per-tick sequence, so keys that come and go
    // shift every later variable's slot and silently corrupt the delta. Bags travel as their own RPC
    // (rpc/state_bag_sync.h), flushed once per tick by ReplicationManager.
    class StateBag final {
      public:
        struct Entry {
            StateValue value;
            StateScope scope = StateScope::Broadcast;
        };

        // Every write crosses the network to every viewer of the entity, so an unbounded bag is a
        // denial of service one careless resource away: a few hundred keys of a megabyte each is all
        // it takes. Raising a limit later is safe; lowering one breaks live gamemodes.
        static constexpr size_t kMaxKeyLength  = 64;
        static constexpr size_t kMaxValueBytes = 4096;
        static constexpr size_t kMaxKeys       = 128;

        enum class WriteResult : uint8_t {
            Applied = 0,
            Unchanged, // Same value and scope already stored; nothing sent, nothing notified.
            KeyTooLong,
            ValueTooLarge,
            TooManyKeys,
        };

        static const char *WriteResultToString(WriteResult result);

        // Bound once by NetworkEntity's constructor; a bag never exists detached from its entity.
        void Bind(NetworkEntity *owner) {
            _owner = owner;
        }

        const StateValue *Get(const std::string &key) const;
        bool Has(const std::string &key) const {
            return Get(key) != nullptr;
        }
        StateScope ScopeOf(const std::string &key) const;
        std::vector<std::string> Keys() const;
        size_t Size() const {
            return _entries.size();
        }
        bool Empty() const {
            return _entries.empty();
        }

        // Marks the key dirty for the next flush and raises the change notification immediately, so a
        // script observes its own write in the tick it made it.
        WriteResult Set(const std::string &key, const StateValue &value, StateScope scope = StateScope::Broadcast);
        bool Remove(const std::string &key);

        // --- Replication plumbing ---

        // What a dirty key has to tell the wire: who it reaches now, and who it reached before.
        //
        // Both, because a key's audience can shrink. A key that was Broadcast and is now Owner or
        // Server has already been delivered to connections that must no longer hold it, and they only
        // drop it if they are sent a removal. `previous` is what says who those are; it is empty when
        // the key did not exist before this tick's first write.
        //
        // Recorded here rather than read back at flush time because a removed key has no entry left
        // to read a scope from.
        struct DirtyEntry {
            StateScope scope = StateScope::Broadcast;
            std::optional<StateScope> previous;
        };

        const std::unordered_map<std::string, DirtyEntry> &Dirty() const {
            return _dirty;
        }
        bool HasDirty() const {
            return !_dirty.empty();
        }
        void ClearDirty() {
            _dirty.clear();
        }

        // Construction seed, written per destination: Broadcast always, Owner only to the owner,
        // Server never.
        void SerializeSeed(MafiaNet::BitStream *bs, bool toOwner) const;

        // Replaces the bag with what a seed carried. Returns the keys it set, for the caller to
        // notify once the entity is fully constructed.
        std::vector<std::string> DeserializeSeed(MafiaNet::BitStream *bs);

        // Applies one inbound change from the server. The server is the only writer, so the limits it
        // already enforced are not re-checked here.
        void Apply(const std::string &key, const StateValue &value, bool removed);

        // For the re-seed an ownership change needs: the incoming owner has never been sent these.
        std::vector<std::string> OwnerKeys() const;

      private:
        void MarkDirty(const std::string &key, StateScope scope, std::optional<StateScope> previous);
        void Notify(const std::string &key, const StateValue &value, const StateValue &previous, bool hadPrevious, bool removed);

        std::unordered_map<std::string, Entry> _entries;
        std::unordered_map<std::string, DirtyEntry> _dirty;
        NetworkEntity *_owner = nullptr;
    };
} // namespace Framework::Networking::Replication
