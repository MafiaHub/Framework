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
#include <vector>

namespace Framework::Networking::Replication {
    namespace {
        thread_local size_t t_held = 0;

        struct Doomed {
            World *world;
            std::shared_ptr<Detail::WorldEntitySlot> slot;
        };
        thread_local std::vector<Doomed> t_doomed;

        // Locks are ordered within one call only, so a second call could wait on a thread that waits
        // on this one. Fatal in every build type: a deadlock on a live server is worse.
        void RequireNoneHeld(const char *what) {
            if (t_held == 0) {
                return;
            }
            const auto logger = Logging::GetLogger(FRAMEWORK_INNER_NETWORKING);
            logger->critical("World: {} while this thread holds {} handle(s); take everything a scope needs in one call", what, t_held);
            logger->flush();
            std::abort();
        }
    } // namespace

    World::World(ReplicationManager &manager): _manager(manager) {}
    World::~World() = default;

    size_t World::HeldByThisThread() {
        return t_held;
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
                if (it == _slots.end() || it->second->entity->GetTypeId() != request.typeId) {
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
            ++t_held;
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
        --t_held;
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
        // comes back here.
        while (t_held == 0 && !t_doomed.empty()) {
            const Doomed next = std::move(t_doomed.front());
            t_doomed.erase(t_doomed.begin());
            next.world->Teardown(next.slot);
        }
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

    NetworkEntity *World::CreateEntity(uint32_t typeId, Detail::WorldHold &hold) {
        RequireNoneHeld("an entity was created");
        if (!_manager.IsServer()) {
            return nullptr;
        }
        return _manager.CreateEntity(typeId, &hold);
    }

    bool World::Destroy(uint64_t id) {
        std::shared_ptr<Detail::WorldEntitySlot> slot;
        bool unheld = false;
        {
            const std::lock_guard guard(_slotsMutex);
            const auto it = _slots.find(id);
            if (it == _slots.end()) {
                return false;
            }
            slot = std::move(it->second);
            _slots.erase(it);
            slot->destroyed = true;
            _doomed.emplace(id, slot);
            _doomedCount.fetch_add(1, std::memory_order_release);
            unheld = slot->holders == 0;
        }
        if (unheld) {
            t_doomed.push_back({this, std::move(slot)});
            FinishDoomed();
        }
        return true;
    }

    std::vector<uint64_t> World::IdsOfType(uint32_t typeId) {
        std::vector<uint64_t> ids;
        {
            const std::lock_guard guard(_slotsMutex);
            for (const auto &[id, slot] : _slots) {
                if (slot->entity->GetTypeId() == typeId) {
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

    void World::Adopt(NetworkEntity *entity, Detail::WorldHold *hold) {
        auto slot    = std::make_shared<Detail::WorldEntitySlot>();
        slot->entity = entity;
        slot->id     = entity->GetNetworkID();
        if (hold) {
            slot->mutex.lock();
            slot->holders = 1;
            ++t_held;
            *hold = Detail::WorldHold(this, slot.get(), slot, true);
        }
        const std::lock_guard guard(_slotsMutex);
        _slots[slot->id] = std::move(slot);
    }

    void World::Forget(NetworkEntity *entity) {
        const std::lock_guard guard(_slotsMutex);
        const auto it = _slots.find(entity->GetNetworkID());
        if (it != _slots.end() && it->second->entity == entity) {
            _slots.erase(it);
        }
    }
} // namespace Framework::Networking::Replication
