/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "world.h"

#include "replication_manager.h"

#include <logging/logger.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <exception>
#include <vector>

namespace Framework::Networking::Replication {
    namespace {
        // The locks this thread holds, handles and guards alike; a handful at most.
        thread_local std::vector<const Detail::WorldLockable *> t_locks;

        struct Doomed {
            World *world;
            std::shared_ptr<Detail::WorldEntitySlot> slot;
        };
        thread_local std::deque<Doomed> t_doomed;
        // What this thread put off until it holds nothing (World::WhenNoneHeld), oldest first.
        thread_local std::deque<fu2::unique_function<void()>> t_deferred;
        // Set while this thread tears one down or runs one of those: a handle taken and released
        // inside must not start the next.
        thread_local bool t_draining = false;
        // World::HoldOff scopes alive on this thread.
        thread_local uint32_t t_heldOff = 0;
        // World::Creating scopes alive on this thread.
        thread_local uint32_t t_creating = 0;

        void Unhold(const Detail::WorldLockable *lock) {
            const auto it = std::find(t_locks.begin(), t_locks.end(), lock);
            if (it != t_locks.end()) {
                *it = t_locks.back();
                t_locks.pop_back();
            }
        }
    } // namespace

    // Locks are ordered within one call only, so a second call could wait on a thread that waits on
    // this one. Fatal in every build type: a deadlock on a live server is worse.
    void World::RequireNoneHeld(const char *what) {
        if (t_locks.empty()) {
            return;
        }
        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_NETWORKING);
        logger->critical("World: {} while this thread holds {} handle(s); take everything a scope needs in one call", what, t_locks.size());
        logger->flush();
        std::abort();
    }

    void World::RequireWorldFree(const char *what) {
        RequireNoneHeld(what);
        if (t_creating == 0) {
            return;
        }
        const auto logger = Logging::GetLogger(FRAMEWORK_INNER_NETWORKING);
        logger->critical("World: {} from inside an entity's creation callback, which runs with the new entity locked", what);
        logger->flush();
        std::abort();
    }

    World::Creating::Creating() {
        ++t_creating;
    }

    World::Creating::~Creating() {
        --t_creating;
    }

    namespace Detail {
        EntityGuard::EntityGuard(const NetworkEntity &entity, bool exclusive): _exclusive(exclusive) {
            WorldLockable *lock = entity._worldLock;
            if (lock == nullptr || std::find(t_locks.begin(), t_locks.end(), lock) != t_locks.end()) {
                return;
            }
            if (exclusive) {
                lock->mutex.lock();
            }
            else {
                lock->mutex.lock_shared();
            }
            t_locks.push_back(lock);
            _lock = lock;
        }

        EntityGuard::~EntityGuard() {
            if (_lock == nullptr) {
                return;
            }
            Unhold(_lock);
            if (_exclusive) {
                _lock->mutex.unlock();
            }
            else {
                _lock->mutex.unlock_shared();
            }
        }
    } // namespace Detail

    World::World(ReplicationManager &manager): _manager(manager) {}
    World::~World() = default;

    size_t World::HeldByThisThread() {
        return t_locks.size();
    }

    void World::Acquire(Detail::WorldRequest *requests, size_t count, bool exclusive) {
        RequireNoneHeld("a handle was asked for");

        Detail::WorldRequest *resolved[kMaxAtOnce];
        size_t resolvedCount = 0;
        const auto asked     = [&](const Detail::WorldRequest &request) {
            return std::any_of(resolved, resolved + resolvedCount, [&](const Detail::WorldRequest *earlier) {
                return earlier->lock == request.lock;
            });
        };

        for (size_t i = 0; i < count; ++i) {
            Detail::WorldRequest &request = requests[i];
            if (!request.isState) {
                continue;
            }
            for (size_t index = 0; index < _states.size(); ++index) {
                if (_states[index]->key == request.stateKey) {
                    request.lock = _states[index].get();
                    if (asked(request)) {
                        request.lock = nullptr;
                        break;
                    }
                    request.state             = _states[index]->object.get();
                    request.order             = index;
                    resolved[resolvedCount++] = &request;
                    break;
                }
            }
        }
        {
            // Counted as holders here, before waiting for their locks, so that none of them can be
            // torn down in between.
            const std::lock_guard guard(_slotsMutex);
            for (size_t i = 0; i < count; ++i) {
                Detail::WorldRequest &request = requests[i];
                if (request.isState) {
                    continue;
                }
                const auto it = _slots.find(request.id);
                if (it == _slots.end() || (!request.anyType && it->second->entity->GetTypeId() != request.typeId)) {
                    continue;
                }
                request.lock = it->second.get();
                if (asked(request)) {
                    request.lock = nullptr;
                    continue;
                }
                ++it->second->holders;
                request.slot              = it->second;
                request.entity            = it->second->entity;
                request.order             = request.id;
                resolved[resolvedCount++] = &request;
            }
        }

        std::sort(resolved, resolved + resolvedCount, [](const Detail::WorldRequest *a, const Detail::WorldRequest *b) {
            if (a->isState != b->isState) {
                return a->isState;
            }
            return a->order < b->order;
        });
        for (size_t i = 0; i < resolvedCount; ++i) {
            Detail::WorldRequest &request = *resolved[i];
            if (exclusive) {
                request.lock->mutex.lock();
            }
            else {
                request.lock->mutex.lock_shared();
            }
            t_locks.push_back(request.lock);
            if (request.slot && request.slot->destroyed) {
                // Destroyed while this thread waited for it.
                Release(request.lock, request.slot, exclusive);
                request.slot.reset();
                request.entity = nullptr;
                continue;
            }
            request.hold = Detail::WorldHold(this, request.lock, std::move(request.slot), exclusive);
        }
    }

    void World::Release(Detail::WorldLockable *lock, const std::shared_ptr<Detail::WorldEntitySlot> &slot, bool exclusive) {
        if (exclusive) {
            lock->mutex.unlock();
        }
        else {
            lock->mutex.unlock_shared();
        }
        Unhold(lock);
        if (slot) {
            bool last = false;
            {
                const std::lock_guard guard(_slotsMutex);
                last = --slot->holders == 0 && slot->destroyed;
            }
            if (last) {
                t_doomed.push_back({this, slot});
            }
        }
        FinishDoomed();
    }

    void World::FinishDoomed() {
        // One at a time and off the list first: a teardown may take and release handles, which
        // comes back here and leaves the rest to this loop.
        if (t_draining || t_heldOff != 0) {
            return;
        }
        struct Draining {
            Draining() {
                t_draining = true;
            }
            ~Draining() {
                t_draining = false;
            }
        } draining;
        while (t_locks.empty() && (!t_deferred.empty() || !t_doomed.empty())) {
            // This runs from a handle's release, which is a destructor: what is thrown here has
            // nowhere to go. It is logged, and the rest still runs.
            try {
                if (!t_deferred.empty()) {
                    fu2::unique_function<void()> next = std::move(t_deferred.front());
                    t_deferred.pop_front();
                    next();
                    continue;
                }
                const Doomed next = std::move(t_doomed.front());
                t_doomed.pop_front();
                next.world->Teardown(next.slot);
            }
            catch (const std::exception &error) {
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("World: what was put off until nothing was held threw: {}", error.what());
            }
            catch (...) {
                Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("World: what was put off until nothing was held threw");
            }
        }
    }

    World::HoldOff::HoldOff() {
        ++t_heldOff;
    }

    World::HoldOff::~HoldOff() {
        --t_heldOff;
    }

    void World::WhenNoneHeld(fu2::unique_function<void()> fn) {
        t_deferred.push_back(std::move(fn));
        FinishDoomed();
    }

    void World::Settle() {
        FinishDoomed();
    }

    void World::Teardown(const std::shared_ptr<Detail::WorldEntitySlot> &slot) {
        _manager.TeardownEntity(slot->entity);
        const std::lock_guard guard(_slotsMutex);
        _doomed.erase(slot->id);
        _doomedCount.fetch_sub(1, std::memory_order_release);
    }

    bool World::IsDoomed(uint64_t id) const {
        if (!AnyDoomed()) {
            return false;
        }
        const std::lock_guard guard(_slotsMutex);
        return _doomed.find(id) != _doomed.end();
    }

    bool World::Exists(uint64_t id) const {
        const std::lock_guard guard(_slotsMutex);
        return _slots.find(id) != _slots.end();
    }

    NetworkEntity *World::CreateEntity(uint32_t typeId, Detail::WorldHold &hold) {
        RequireWorldFree("an entity was created");
        if (!_manager.IsServer()) {
            return nullptr;
        }
        return _manager.CreateEntity(typeId, hold);
    }

    bool World::Destroy(uint64_t id) {
        std::shared_ptr<Detail::WorldEntitySlot> unheld;
        const Doom doom = MarkDoomed(id, unheld);
        if (unheld) {
            TeardownUnheld(std::move(unheld));
        }
        return doom == Doom::Marked;
    }

    World::Doom World::MarkDoomed(uint64_t id, std::shared_ptr<Detail::WorldEntitySlot> &unheld) {
        const std::lock_guard guard(_slotsMutex);
        const auto it = _slots.find(id);
        if (it == _slots.end()) {
            return _doomed.find(id) != _doomed.end() ? Doom::Pending : Doom::Unknown;
        }
        std::shared_ptr<Detail::WorldEntitySlot> slot = std::move(it->second);
        _slots.erase(it);
        slot->destroyed = true;
        _doomed.emplace(id, slot);
        _doomedCount.fetch_add(1, std::memory_order_release);
        if (slot->holders == 0) {
            unheld = std::move(slot);
        }
        return Doom::Marked;
    }

    void World::TeardownUnheld(std::shared_ptr<Detail::WorldEntitySlot> slot) {
        t_doomed.push_back({this, std::move(slot)});
        FinishDoomed();
    }

    std::vector<uint64_t> World::IdsOfType(uint32_t typeId, bool anyType) {
        std::vector<uint64_t> ids;
        {
            const std::lock_guard guard(_slotsMutex);
            for (const auto &[id, slot] : _slots) {
                if (anyType || slot->entity->GetTypeId() == typeId) {
                    ids.push_back(id);
                }
            }
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    void World::AddState(const void *key, void *object, void (*destroy)(void *)) {
        std::unique_ptr<void, void (*)(void *)> owned(object, destroy);
        for (const auto &state : _states) {
            if (state->key == key) {
                state->object = std::move(owned);
                return;
            }
        }
        auto state    = std::make_unique<StateSlot>();
        state->key    = key;
        state->object = std::move(owned);
        _states.push_back(std::move(state));
    }

    std::shared_ptr<Detail::WorldEntitySlot> World::Prepare(NetworkEntity *entity) {
        auto slot    = std::make_shared<Detail::WorldEntitySlot>();
        slot->entity = entity;
        slot->id     = entity->GetNetworkID();
        // Locked for its creator before anything can reach the entity: whoever finds it through
        // ReplicaManager3 from here on waits on this lock.
        slot->mutex.lock();
        slot->holders      = 1;
        entity->_worldLock = slot.get();
        return slot;
    }

    void World::Publish(std::shared_ptr<Detail::WorldEntitySlot> slot, Detail::WorldHold &hold) {
        t_locks.push_back(slot.get());
        hold = Detail::WorldHold(this, slot.get(), slot, true);
        const std::lock_guard guard(_slotsMutex);
        _slots[slot->id] = std::move(slot);
    }

    void World::Adopt(NetworkEntity *entity) {
        auto slot    = std::make_shared<Detail::WorldEntitySlot>();
        slot->entity = entity;
        slot->id     = entity->GetNetworkID();
        const std::lock_guard guard(_slotsMutex);
        _slots[slot->id] = std::move(slot);
    }

    void World::Forget(NetworkEntity *entity) {
        const std::lock_guard guard(_slotsMutex);
        const auto it = _slots.find(entity->GetNetworkID());
        if (it != _slots.end() && it->second->entity == entity) {
            _slots.erase(it);
            entity->_worldLock = nullptr;
        }
    }
} // namespace Framework::Networking::Replication
