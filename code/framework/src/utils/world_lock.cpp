/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "world_lock.h"

#include <utility>

namespace Framework::Utils {
    void WorldLock::SetContendedHook(fu2::function<void()> hook) {
        std::lock_guard lock(_mutex);
        _contended = std::move(hook);
    }

    void WorldLock::LockForTick() {
        std::unique_lock lock(_mutex);
        if (_owner != Owner::None) {
            ++_ticksWaiting;
            if (_owner == Owner::Script && _contended) {
                // Outside the mutex: the hook reaches into the script side, which may be about to take it.
                auto hook = _contended;
                lock.unlock();
                hook();
                lock.lock();
            }
            _changed.wait(lock, [this] {
                return _owner == Owner::None;
            });
            --_ticksWaiting;
        }
        _owner = Owner::Tick;
        // A script side waiting for no tick to be waiting.
        _changed.notify_all();
    }

    void WorldLock::UnlockFromTick() {
        {
            std::lock_guard lock(_mutex);
            _owner = Owner::None;
        }
        _changed.notify_all();
    }

    void WorldLock::EvictScript() {
        std::unique_lock lock(_mutex);
        ++_evictions;
        if (!_scriptInTask) {
            return;
        }
        // It stopped mid-task for this tick. Hand the world back so it can finish the task; it leaves at its next
        // checkpoint, where it sees the eviction.
        _owner = Owner::None;
        _changed.notify_all();
        _changed.wait(lock, [this] {
            return !_scriptInTask && _owner == Owner::None;
        });
        _owner = Owner::Tick;
    }

    void WorldLock::ReadmitScript() {
        {
            std::lock_guard lock(_mutex);
            --_evictions;
        }
        _changed.notify_all();
    }

    bool WorldLock::LockForScript() {
        std::unique_lock lock(_mutex);
        _changed.wait(lock, [this] {
            return _closed || (_owner == Owner::None && _ticksWaiting == 0 && _evictions == 0);
        });
        if (_closed) {
            return false;
        }
        _owner        = Owner::Script;
        _scriptInTask = true;
        return true;
    }

    void WorldLock::UnlockFromScript() {
        {
            std::lock_guard lock(_mutex);
            _owner        = Owner::None;
            _scriptInTask = false;
        }
        _changed.notify_all();
    }

    bool WorldLock::YieldToTick() {
        std::unique_lock lock(_mutex);
        if (_ticksWaiting == 0) {
            return false;
        }
        // Still in its task: only the world changes hands.
        _owner = Owner::None;
        _changed.notify_all();
        // An eviction does not hold it back here: the tick that evicts is waiting for this very task to finish.
        _changed.wait(lock, [this] {
            return _owner == Owner::None && _ticksWaiting == 0;
        });
        _owner = Owner::Script;
        return true;
    }

    WorldLock::Handoff WorldLock::Checkpoint() {
        {
            std::lock_guard lock(_mutex);
            if (_ticksWaiting == 0 && _evictions == 0) {
                return Handoff::None;
            }
        }
        UnlockFromScript();
        return LockForScript() ? Handoff::Yielded : Handoff::Closed;
    }

    bool WorldLock::TickWaiting() const {
        std::lock_guard lock(_mutex);
        return _ticksWaiting != 0;
    }

    void WorldLock::Close() {
        {
            std::lock_guard lock(_mutex);
            _closed = true;
        }
        _changed.notify_all();
    }
} // namespace Framework::Utils
