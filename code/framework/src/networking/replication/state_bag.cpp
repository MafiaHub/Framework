/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "state_bag.h"

#include "network_entity.h"

#include <algorithm>

namespace Framework::Networking::Replication {
    void StateValue::Serialize(MafiaNet::BitStream *bs, bool write) {
        auto tag = static_cast<uint8_t>(type);
        bs->Serialize(write, tag);
        if (!write) {
            // An unknown tag degrades to Null instead of aliasing some other case.
            type = tag <= static_cast<uint8_t>(Type::Json) ? static_cast<Type>(tag) : Type::Null;
        }

        switch (type) {
        case Type::Boolean: bs->Serialize(write, boolean); break;
        case Type::Number: bs->Serialize(write, number); break;
        case Type::String:
        case Type::Json: bs->Serialize(write, text); break;
        case Type::Null: break;
        }
    }

    const char *StateBag::WriteResultToString(WriteResult result) {
        switch (result) {
        case WriteResult::Applied: return "applied";
        case WriteResult::Unchanged: return "unchanged";
        case WriteResult::KeyTooLong: return "key is longer than 64 characters";
        case WriteResult::ValueTooLarge: return "value is larger than 4096 bytes";
        case WriteResult::TooManyKeys: return "bag already holds 128 keys";
        }
        return "unknown";
    }

    const StateValue *StateBag::Get(const std::string &key) const {
        const auto it = _entries.find(key);
        return it != _entries.end() ? &it->second.value : nullptr;
    }

    StateScope StateBag::ScopeOf(const std::string &key) const {
        const auto it = _entries.find(key);
        return it != _entries.end() ? it->second.scope : StateScope::Broadcast;
    }

    std::vector<std::string> StateBag::Keys() const {
        std::vector<std::string> keys;
        keys.reserve(_entries.size());
        for (const auto &[key, entry] : _entries) {
            keys.push_back(key);
        }
        // Scripts enumerate this, and the backing store's order is arbitrary and unstable.
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    std::vector<std::string> StateBag::OwnerKeys() const {
        std::vector<std::string> keys;
        for (const auto &[key, entry] : _entries) {
            if (entry.scope == StateScope::Owner) {
                keys.push_back(key);
            }
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    std::vector<std::string> StateBag::OwnerRevokeKeys() const {
        std::vector<std::string> keys = OwnerKeys();
        for (const auto &[key, dirty] : _dirty) {
            // Only where the key has stopped reaching any client. One narrowed to Broadcast is still
            // going out to everyone, the old owner included, so it needs no revocation.
            if (!dirty.previous.has_value() || *dirty.previous != StateScope::Owner || dirty.scope != StateScope::Server) {
                continue;
            }
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
                keys.push_back(key);
            }
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    StateBag::WriteResult StateBag::Set(const std::string &key, const StateValue &value, StateScope scope) {
        if (key.empty() || key.size() > kMaxKeyLength) {
            return WriteResult::KeyTooLong;
        }
        if (value.ByteSize() > kMaxValueBytes) {
            return WriteResult::ValueTooLarge;
        }

        const auto it = _entries.find(key);
        if (it == _entries.end() && _entries.size() >= kMaxKeys) {
            return WriteResult::TooManyKeys;
        }

        StateValue previous;
        const bool hadPrevious = it != _entries.end();
        std::optional<StateScope> previousScope;
        if (hadPrevious) {
            if (it->second.value == value && it->second.scope == scope) {
                return WriteResult::Unchanged;
            }
            previous      = it->second.value;
            previousScope = it->second.scope;
        }

        _entries[key] = Entry {value, scope};

        // Server scope is storage, not replication -- but a key that *was* replicated and is now
        // Server still has to be taken back off the peers holding it, so the write dirties whenever
        // either side of the transition reaches the wire.
        const bool reachesWire  = scope != StateScope::Server;
        const bool reachedWire  = previousScope.has_value() && *previousScope != StateScope::Server;
        if (reachesWire || reachedWire) {
            MarkDirty(key, scope, previousScope);
        }
        Notify(key, value, previous, hadPrevious, false);
        return WriteResult::Applied;
    }

    bool StateBag::Remove(const std::string &key) {
        const auto it = _entries.find(key);
        if (it == _entries.end()) {
            return false;
        }

        const StateValue previous = it->second.value;
        const StateScope scope    = it->second.scope;
        _entries.erase(it);

        // A removal reaches nobody and reached whoever the key was scoped to, which is exactly the
        // shape the flush already handles: everyone leaving the audience is sent a removal.
        if (scope != StateScope::Server) {
            MarkDirty(key, StateScope::Server, scope);
        }
        Notify(key, StateValue {}, previous, true, true);
        return true;
    }

    void StateBag::Apply(const std::string &key, const StateValue &value, bool removed) {
        if (removed) {
            const auto it = _entries.find(key);
            if (it == _entries.end()) {
                return;
            }
            const StateValue previous = it->second.value;
            _entries.erase(it);
            Notify(key, StateValue {}, previous, true, true);
            return;
        }

        StateValue previous;
        const auto it          = _entries.find(key);
        const bool hadPrevious = it != _entries.end();
        if (hadPrevious) {
            previous = it->second.value;
        }
        // Scope is a server-side routing decision; a receiver keeps whatever it was sent.
        _entries[key] = Entry {value, StateScope::Broadcast};
        Notify(key, value, previous, hadPrevious, false);
    }

    void StateBag::SerializeSeed(MafiaNet::BitStream *bs, bool toOwner) const {
        uint16_t count = 0;
        for (const auto &[key, entry] : _entries) {
            if (entry.scope == StateScope::Broadcast || (toOwner && entry.scope == StateScope::Owner)) {
                ++count;
            }
        }
        bs->Write(count);

        for (const auto &[key, entry] : _entries) {
            if (entry.scope != StateScope::Broadcast && !(toOwner && entry.scope == StateScope::Owner)) {
                continue;
            }
            bs->Write(key);
            // Serialize takes a mutable reference and the entry is const here.
            StateValue value = entry.value;
            value.Serialize(bs, true);
        }
    }

    std::vector<std::string> StateBag::DeserializeSeed(MafiaNet::BitStream *bs) {
        _entries.clear();
        _dirty.clear();

        uint16_t count = 0;
        bs->Read(count);

        std::vector<std::string> keys;
        // The sender never writes more than kMaxKeys, so a larger count is a corrupt or hostile
        // stream. Bound it rather than allocate on it.
        const uint16_t bounded = static_cast<uint16_t>(std::min<size_t>(count, kMaxKeys));
        keys.reserve(bounded);
        for (uint16_t i = 0; i < bounded; ++i) {
            std::string key;
            if (!bs->Read(key)) {
                break;
            }
            StateValue value;
            value.Serialize(bs, false);
            if (key.empty() || key.size() > kMaxKeyLength || value.ByteSize() > kMaxValueBytes) {
                continue;
            }
            _entries[key] = Entry {value, StateScope::Broadcast};
            keys.push_back(key);
        }
        return keys;
    }

    void StateBag::MarkDirty(const std::string &key, StateScope scope, std::optional<StateScope> previous) {
        const auto existing = _dirty.find(key);
        if (existing != _dirty.end()) {
            // Several writes in one tick collapse to one change, and the audience that matters is the
            // one from before the first of them -- that is who is holding a stale value.
            existing->second.scope = scope;
        }
        else {
            _dirty.emplace(key, DirtyEntry {scope, previous});
        }

        if (_owner != nullptr) {
            _owner->MarkStateDirty();
        }
    }

    void StateBag::Notify(const std::string &key, const StateValue &value, const StateValue &previous, bool hadPrevious, bool removed) {
        if (_owner == nullptr) {
            return;
        }
        StateChange change;
        change.entity      = _owner;
        change.key         = key;
        change.value       = value;
        change.previous    = previous;
        change.hadPrevious = hadPrevious;
        change.removed     = removed;
        _owner->NotifyStateChanged(change);
    }
} // namespace Framework::Networking::Replication
