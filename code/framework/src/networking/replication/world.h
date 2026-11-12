/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "entity_registry.h"
#include "network_entity.h"

#include <function2/function2.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Framework::Networking::Replication {
    class ReplicationManager;
    class World;

    namespace Detail {
        struct WorldLockable {
            std::shared_mutex mutex;
        };

        // The manager's bookkeeping for one entity: its lock lives here rather than in NetworkEntity,
        // which the client shares. Everything but the lock is guarded by the world's slot mutex.
        struct WorldEntitySlot final: WorldLockable {
            NetworkEntity *entity = nullptr;
            uint64_t id           = 0;
            // Handles on the entity, counted from before they wait for its lock.
            uint32_t holders = 0;
            // Set once, under the slot mutex; read without it by a thread that waited for the lock.
            std::atomic<bool> destroyed {false};
        };

        // One lock held for a handle, released with it.
        class WorldHold final {
          public:
            WorldHold() = default;
            WorldHold(World *world, WorldLockable *lock, std::shared_ptr<WorldEntitySlot> slot, bool exclusive): _world(world), _lock(lock), _slot(std::move(slot)), _exclusive(exclusive) {}
            WorldHold(WorldHold &&other) noexcept: _world(other._world), _lock(std::exchange(other._lock, nullptr)), _slot(std::move(other._slot)), _exclusive(other._exclusive) {}
            WorldHold &operator=(WorldHold &&other) noexcept {
                if (this != &other) {
                    Release();
                    _world     = other._world;
                    _lock      = std::exchange(other._lock, nullptr);
                    _slot      = std::move(other._slot);
                    _exclusive = other._exclusive;
                }
                return *this;
            }
            WorldHold(const WorldHold &)            = delete;
            WorldHold &operator=(const WorldHold &) = delete;
            ~WorldHold() {
                Release();
            }

          private:
            void Release();

            World *_world        = nullptr;
            WorldLockable *_lock = nullptr;
            std::shared_ptr<WorldEntitySlot> _slot;
            bool _exclusive = false;
        };

        // One entity or registered state asked of World::Acquire, and what it got.
        struct WorldRequest {
            bool isState         = false;
            uint32_t typeId      = 0;
            uint64_t id          = 0;
            const void *stateKey = nullptr;

            NetworkEntity *entity = nullptr;
            void *state           = nullptr;
            // Position in the lock order among requests of the same kind.
            uint64_t order      = 0;
            WorldLockable *lock = nullptr;
            std::shared_ptr<WorldEntitySlot> slot;
            WorldHold hold;
        };
    } // namespace Detail

    // A write handle on an entity or a registered state: holds its lock for as long as it lives.
    // Empty when what was asked for does not resolve. Keep it in the scope that took it; what is kept
    // across calls is the id.
    template <typename T>
    class Ref final {
      public:
        Ref() = default;
        Ref(Ref &&other) noexcept: _object(std::exchange(other._object, nullptr)), _hold(std::move(other._hold)) {}
        Ref &operator=(Ref &&other) noexcept {
            if (this != &other) {
                _object = std::exchange(other._object, nullptr);
                _hold   = std::move(other._hold);
            }
            return *this;
        }
        Ref(const Ref &)            = delete;
        Ref &operator=(const Ref &) = delete;

        explicit operator bool() const {
            return _object != nullptr;
        }
        T *operator->() const {
            return _object;
        }
        T &operator*() const {
            return *_object;
        }

      private:
        friend class World;
        Ref(T *object, Detail::WorldHold &&hold): _object(object), _hold(std::move(hold)) {}

        T *_object = nullptr;
        Detail::WorldHold _hold;
    };

    // The read handle: shares the lock with other readers.
    template <typename T>
    class ConstRef final {
      public:
        ConstRef() = default;
        ConstRef(ConstRef &&other) noexcept: _object(std::exchange(other._object, nullptr)), _hold(std::move(other._hold)) {}
        ConstRef &operator=(ConstRef &&other) noexcept {
            if (this != &other) {
                _object = std::exchange(other._object, nullptr);
                _hold   = std::move(other._hold);
            }
            return *this;
        }
        ConstRef(const ConstRef &)            = delete;
        ConstRef &operator=(const ConstRef &) = delete;

        explicit operator bool() const {
            return _object != nullptr;
        }
        const T *operator->() const {
            return _object;
        }
        const T &operator*() const {
            return *_object;
        }

      private:
        friend class World;
        ConstRef(const T *object, Detail::WorldHold &&hold): _object(object), _hold(std::move(hold)) {}

        const T *_object = nullptr;
        Detail::WorldHold _hold;
    };

    // Server: how every thread reaches replicated entities, and the mod state registered beside
    // them. A type deriving from NetworkEntity is asked for by network id; any other type is
    // registered state, of which there is one, asked for without an id.
    //
    // A thread takes everything it needs in one call. Locks are taken in a fixed order (states as
    // registered, then entities by ascending network id), so two threads asking for the same things
    // in different orders cannot deadlock. Asking again while a handle is held logs and aborts, in
    // every build type.
    class World final {
      public:
        // How many entities and states one call can take.
        static constexpr size_t kMaxAtOnce = 8;

        explicit World(ReplicationManager &manager);
        ~World();
        World(const World &)            = delete;
        World &operator=(const World &) = delete;

        template <typename T>
        ConstRef<T> Read(uint64_t id) {
            static_assert(std::is_base_of_v<NetworkEntity, T>, "World::Read<T>(id): T must derive from NetworkEntity");
            return std::get<0>(AcquireAll<false, T>(&id));
        }
        template <typename T>
        ConstRef<T> Read() {
            static_assert(!std::is_base_of_v<NetworkEntity, T>, "World::Read<T>(): an entity is asked for by id");
            return std::get<0>(AcquireAll<false, T>(nullptr));
        }
        // Several at once: one id per entity type in order, none for state types. An id asked for
        // twice fills the first handle only.
        template <typename A, typename B, typename... Rest, typename... Ids>
        std::tuple<ConstRef<A>, ConstRef<B>, ConstRef<Rest>...> Read(Ids... ids) {
            static_assert(sizeof...(Ids) == kEntityCount<A, B, Rest...>, "World::Read: one id per entity type");
            const uint64_t list[sizeof...(Ids) + 1] = {static_cast<uint64_t>(ids)..., 0};
            return AcquireAll<false, A, B, Rest...>(list);
        }

        template <typename T>
        Ref<T> Write(uint64_t id) {
            static_assert(std::is_base_of_v<NetworkEntity, T>, "World::Write<T>(id): T must derive from NetworkEntity");
            return std::get<0>(AcquireAll<true, T>(&id));
        }
        template <typename T>
        Ref<T> Write() {
            static_assert(!std::is_base_of_v<NetworkEntity, T>, "World::Write<T>(): an entity is asked for by id");
            return std::get<0>(AcquireAll<true, T>(nullptr));
        }
        template <typename A, typename B, typename... Rest, typename... Ids>
        std::tuple<Ref<A>, Ref<B>, Ref<Rest>...> Write(Ids... ids) {
            static_assert(sizeof...(Ids) == kEntityCount<A, B, Rest...>, "World::Write: one id per entity type");
            const uint64_t list[sizeof...(Ids) + 1] = {static_cast<uint64_t>(ids)..., 0};
            return AcquireAll<true, A, B, Rest...>(list);
        }

        // Visits every T that exists when the call starts, by ascending id, one lock at a time. The
        // callback holds the entity it is given and so cannot ask the world for another.
        template <typename T>
        void ForEach(fu2::function<void(Ref<T> &)> fn) {
            static_assert(std::is_base_of_v<NetworkEntity, T>, "World::ForEach<T>: T must derive from NetworkEntity");
            for (const uint64_t id : IdsOfType(EntityTypeId<T>())) {
                if (Ref<T> entity = Write<T>(id)) {
                    fn(entity);
                }
            }
        }

        // Creates a T and returns it held: no other thread reaches it before the caller lets go.
        // Empty if the type is not registered.
        template <typename T>
        Ref<T> Create() {
            static_assert(std::is_base_of_v<NetworkEntity, T>, "World::Create<T>: T must derive from NetworkEntity");
            Detail::WorldHold hold;
            NetworkEntity *entity = CreateEntity(EntityTypeId<T>(), hold);
            return entity ? Ref<T>(static_cast<T *>(entity), std::move(hold)) : Ref<T>();
        }
        // The id stops resolving before this returns, here and through the manager; false if it did
        // not resolve. The entity itself goes once no handle holds it and the thread that finds so
        // holds no handle at all: now, or when the last of those is released.
        bool Destroy(uint64_t id);

        // One object per type, registered before the server starts and kept for the world's life.
        template <typename T>
        void RegisterState(std::unique_ptr<T> state) {
            static_assert(!std::is_base_of_v<NetworkEntity, T>, "World::RegisterState<T>: entities are made with Create");
            AddState(StateKey<T>(), state.release(), [](void *object) {
                delete static_cast<T *>(object);
            });
        }

        // Handles the calling thread holds, on any world.
        static size_t HeldByThisThread();

      private:
        friend class ReplicationManager;
        friend class Detail::WorldHold;

        template <typename... Ts>
        static constexpr size_t kEntityCount = (size_t {0} + ... + (std::is_base_of_v<NetworkEntity, Ts> ? 1 : 0));

        template <bool kWrite, typename T>
        using Handle = std::conditional_t<kWrite, Ref<T>, ConstRef<T>>;

        // Writable, so that no linker folds the keys of two types into one address.
        template <typename T>
        static const void *StateKey() {
            static char key = 0;
            return &key;
        }

        template <typename T>
        static Detail::WorldRequest RequestFor(const uint64_t *&nextId) {
            Detail::WorldRequest request;
            if constexpr (std::is_base_of_v<NetworkEntity, T>) {
                request.typeId = EntityTypeId<T>();
                request.id     = *nextId++;
            }
            else {
                request.isState  = true;
                request.stateKey = StateKey<T>();
            }
            return request;
        }

        template <bool kWrite, typename T>
        static Handle<kWrite, T> Take(Detail::WorldRequest &request) {
            if constexpr (std::is_base_of_v<NetworkEntity, T>) {
                return Handle<kWrite, T>(static_cast<T *>(request.entity), std::move(request.hold));
            }
            else {
                return Handle<kWrite, T>(static_cast<T *>(request.state), std::move(request.hold));
            }
        }

        template <bool kWrite, typename... Ts, size_t... Is>
        static std::tuple<Handle<kWrite, Ts>...> TakeAll(Detail::WorldRequest *requests, std::index_sequence<Is...>) {
            return std::tuple<Handle<kWrite, Ts>...>(Take<kWrite, Ts>(requests[Is])...);
        }

        template <bool kWrite, typename... Ts>
        std::tuple<Handle<kWrite, Ts>...> AcquireAll(const uint64_t *ids) {
            static_assert(sizeof...(Ts) <= kMaxAtOnce, "World: too many handles asked for in one call");
            Detail::WorldRequest requests[sizeof...(Ts)] = {RequestFor<Ts>(ids)...};
            Acquire(requests, sizeof...(Ts), kWrite);
            return TakeAll<kWrite, Ts...>(requests, std::index_sequence_for<Ts...> {});
        }

        // Resolves and locks what the requests name, leaving alone those that do not resolve.
        void Acquire(Detail::WorldRequest *requests, size_t count, bool exclusive);
        void Release(Detail::WorldLockable *lock, const std::shared_ptr<Detail::WorldEntitySlot> &slot, bool exclusive);

        // The new entity, held by `hold`; null if the type is unknown.
        NetworkEntity *CreateEntity(uint32_t typeId, Detail::WorldHold &hold);
        std::vector<uint64_t> IdsOfType(uint32_t typeId);
        void AddState(const void *key, void *object, void (*destroy)(void *));

        // From the manager: an entity it created, taken for `hold` if there is one before any other
        // thread can find it, and one deleted by another way than Destroy.
        void Adopt(NetworkEntity *entity, Detail::WorldHold *hold);
        void Forget(NetworkEntity *entity);

        // Destroyed, and still to be torn down.
        bool AnyDoomed() const {
            return _doomedCount.load(std::memory_order_acquire) != 0;
        }
        bool IsDoomed(uint64_t id) const;
        // Tears down what this thread found nothing holding any more, once it holds nothing itself:
        // the teardown calls out, and what it calls may ask a world for a handle.
        static void FinishDoomed();
        void Teardown(const std::shared_ptr<Detail::WorldEntitySlot> &slot);

        struct StateSlot final: Detail::WorldLockable {
            const void *key = nullptr;
            std::unique_ptr<void, void (*)(void *)> object {nullptr, nullptr};
        };

        ReplicationManager &_manager;
        mutable std::mutex _slotsMutex;
        std::unordered_map<uint64_t, std::shared_ptr<Detail::WorldEntitySlot>> _slots;
        // Out of _slots at Destroy, kept here until torn down so the manager can tell.
        std::unordered_map<uint64_t, std::shared_ptr<Detail::WorldEntitySlot>> _doomed;
        std::atomic<size_t> _doomedCount {0};
        // In registration order, which is their lock order. Fixed once the server runs.
        std::vector<std::unique_ptr<StateSlot>> _states;
    };

    namespace Detail {
        inline void WorldHold::Release() {
            if (_lock) {
                _world->Release(std::exchange(_lock, nullptr), _slot, _exclusive);
                _slot.reset();
            }
        }
    } // namespace Detail
} // namespace Framework::Networking::Replication
