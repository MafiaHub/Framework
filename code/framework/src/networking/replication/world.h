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
    class DelegationManager;
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

        // An entity's lock taken by the Framework around what it does on the entity itself: the
        // pass serializing it, inbound state applied to it. Counted like a handle, so asking the
        // world for one inside aborts, but releasing it never tears anything down. Takes nothing
        // for an entity the world does not hold, or one this thread already holds.
        class EntityGuard final {
          public:
            EntityGuard(const NetworkEntity &entity, bool exclusive);
            ~EntityGuard();
            EntityGuard(const EntityGuard &)            = delete;
            EntityGuard &operator=(const EntityGuard &) = delete;

          private:
            WorldLockable *_lock = nullptr;
            bool _exclusive      = false;
        };

        // One entity or registered state asked of World::Acquire, and what it got.
        struct WorldRequest {
            bool isState         = false;
            uint32_t typeId      = 0;
            uint64_t id          = 0;
            const void *stateKey = nullptr;

            NetworkEntity *entity = nullptr;
            void *state           = nullptr;
            // Any entity type, for a handle on the NetworkEntity base.
            bool anyType = false;
            // Position in the lock order among requests of the same kind.
            uint64_t order      = 0;
            WorldLockable *lock = nullptr;
            std::shared_ptr<WorldEntitySlot> slot;
            WorldHold hold;
        };
    } // namespace Detail

    namespace Detail {
        // NetworkIDObject's getter is not const, though it changes nothing once the id is set.
        template <typename T>
        uint64_t IdOf(const T *entity) {
            static_assert(std::is_base_of_v<NetworkEntity, T>, "Id(): only an entity has a network id");
            return entity != nullptr ? const_cast<NetworkEntity *>(static_cast<const NetworkEntity *>(entity))->GetNetworkID() : 0;
        }
    } // namespace Detail

    // What a ForEach callback may return to end the walk early; one that returns nothing visits all.
    enum class Walk : uint8_t {
        Continue,
        Stop
    };

    // A write handle on an entity or a registered state: holds its lock for as long as it lives.
    // Empty when what was asked for does not resolve. Keep it in the scope that took it; what is kept
    // across calls is the id. It belongs to the thread that took it: the lock is that thread's, and so
    // is the count of what it holds, so a handle is never moved to, or let go on, another thread.
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
        // The entity's network id, 0 for an empty handle. For entity handles only.
        uint64_t Id() const {
            return Detail::IdOf(_object);
        }
        // Lets go now rather than at the end of the scope; the handle is empty afterwards.
        void Reset() {
            _object = nullptr;
            _hold   = Detail::WorldHold();
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
        // The entity's network id, 0 for an empty handle. For entity handles only.
        uint64_t Id() const {
            return Detail::IdOf(_object);
        }
        // Lets go now rather than at the end of the scope; the handle is empty afterwards.
        void Reset() {
            _object = nullptr;
            _hold   = Detail::WorldHold();
        }

      private:
        friend class World;
        ConstRef(const T *object, Detail::WorldHold &&hold): _object(object), _hold(std::move(hold)) {}

        const T *_object = nullptr;
        Detail::WorldHold _hold;
    };

    // Server: how every thread reaches replicated entities, and the mod state registered beside
    // them. A type deriving from NetworkEntity is asked for by network id; any other type is
    // registered state, of which there is one, asked for without an id. Asking for NetworkEntity
    // itself gives the entity whatever its type.
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
        // callback holds the entity it is given and so cannot ask the world for another. It takes a
        // Ref<T> & and returns nothing, or a Walk to stop before the rest.
        template <typename T, typename Fn>
        void ForEach(Fn &&fn) {
            static_assert(std::is_base_of_v<NetworkEntity, T>, "World::ForEach<T>: T must derive from NetworkEntity");
            for (const uint64_t id : IdsOfType(TypeOf<T>(), std::is_same_v<T, NetworkEntity>)) {
                if (Ref<T> entity = Write<T>(id)) {
                    if (Visit(fn, entity) == Walk::Stop) {
                        return;
                    }
                }
            }
        }
        // The same for reading: the callback takes a ConstRef<T> &, and shares each entity with other
        // readers instead of keeping them out.
        template <typename T, typename Fn>
        void ForEachRead(Fn &&fn) {
            static_assert(std::is_base_of_v<NetworkEntity, T>, "World::ForEachRead<T>: T must derive from NetworkEntity");
            for (const uint64_t id : IdsOfType(TypeOf<T>(), std::is_same_v<T, NetworkEntity>)) {
                if (ConstRef<T> entity = Read<T>(id)) {
                    if (Visit(fn, entity) == Walk::Stop) {
                        return;
                    }
                }
            }
        }

        // Creates a T and returns it held: no other thread reaches it before the caller lets go.
        // Empty if the type is not registered.
        template <typename T>
        Ref<T> Create() {
            static_assert(std::is_base_of_v<NetworkEntity, T> && !std::is_same_v<T, NetworkEntity>, "World::Create<T>: T must be a registered entity type");
            Detail::WorldHold hold;
            NetworkEntity *entity = CreateEntity(EntityTypeId<T>(), hold);
            return entity ? Ref<T>(static_cast<T *>(entity), std::move(hold)) : Ref<T>();
        }
        // By registered type id, for a type known only at run time.
        Ref<NetworkEntity> Create(uint32_t typeId) {
            Detail::WorldHold hold;
            NetworkEntity *entity = CreateEntity(typeId, hold);
            return entity ? Ref<NetworkEntity>(entity, std::move(hold)) : Ref<NetworkEntity>();
        }
        // The id stops resolving before this returns, here and through the manager; false if it did
        // not resolve. The entity itself goes once no handle holds it and the thread that finds so
        // holds no handle at all: now, or when the last of those is released. So the teardown (the
        // entity's destructor, SetOnEntityDestroyed, the destruction sent to clients) runs on
        // whichever thread lets go of the last handle, which need not be the one that called Destroy.
        bool Destroy(uint64_t id);

        // Whether the id names an entity now, answered from the table of ids alone: no entity's lock is
        // taken, so it may be asked from inside a handle's scope, and by the time it is read the
        // entity may be gone. For telling a caller its id is wrong, not for deciding to use it.
        bool Exists(uint64_t id) const;

        // One object per type, registered before the server handles packets (an instance's PostInit
        // or PostScriptInit) and kept for the world's life: the list is read by every thread without a
        // lock, so registering once a packet thread has started logs critical and aborts.
        template <typename T>
        void RegisterState(std::unique_ptr<T> state) {
            static_assert(!std::is_base_of_v<NetworkEntity, T>, "World::RegisterState<T>: entities are made with Create");
            AddState(StateKey<T>(), state.release(), [](void *object) {
                delete static_cast<T *>(object);
            });
        }

        // No state is registered from here on: called as a packet thread starts on this world's server.
        void CloseRegistration() {
            _registrationClosed.store(true, std::memory_order_release);
        }

        // Handles the calling thread holds, on any world.
        static size_t HeldByThisThread();
        // Logs and aborts if the calling thread holds one: for what takes locks that come before an
        // entity's, and so cannot be done from inside a handle's scope.
        static void RequireNoneHeld(const char *what);
        // The same for what takes ReplicaManager3's world, and so must also not be called from an
        // entity's creation callback, which runs with the new entity locked for its creator.
        static void RequireWorldFree(const char *what);
        // Runs fn once the calling thread holds nothing: at once if it holds nothing now, otherwise
        // when it releases its last handle, or at its next Settle if what it holds is a guard. The
        // tick thread and the packet thread settle after each pass and each drain, and a delegation
        // pass after it has let go of ReplicaManager3's world. A replication worker never does: from
        // a callback the pass makes on its workers (QuerySerialization, a send), fn never runs.
        static void WhenNoneHeld(fu2::unique_function<void()> fn);
        // Runs what the calling thread put off while it held something, teardowns included. For the
        // loops that call into entities under a guard, between two of those calls.
        static void Settle();

      private:
        friend class ReplicationManager;
        friend class DelegationManager;
        friend class Detail::WorldHold;

        template <typename Fn, typename Handle>
        static Walk Visit(Fn &fn, Handle &entity) {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn &, Handle &>>) {
                fn(entity);
                return Walk::Continue;
            }
            else {
                return fn(entity);
            }
        }

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

        // T's registered id; the base has none and stands for every type.
        template <typename T>
        static constexpr uint32_t TypeOf() {
            if constexpr (std::is_same_v<T, NetworkEntity>) {
                return 0;
            }
            else {
                return EntityTypeId<T>();
            }
        }

        template <typename T>
        static Detail::WorldRequest RequestFor(const uint64_t *&nextId) {
            Detail::WorldRequest request;
            if constexpr (std::is_base_of_v<NetworkEntity, T>) {
                request.typeId  = TypeOf<T>();
                request.anyType = std::is_same_v<T, NetworkEntity>;
                request.id      = *nextId++;
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
        std::vector<uint64_t> IdsOfType(uint32_t typeId, bool anyType);
        void AddState(const void *key, void *object, void (*destroy)(void *));

        // From the manager, for an entity it creates. Prepare gives it its lock, taken for the
        // creator, before the manager lets any other thread find the entity; Publish makes the id
        // resolve and counts the lock among the creator's, as `hold`.
        std::shared_ptr<Detail::WorldEntitySlot> Prepare(NetworkEntity *entity);
        void Publish(std::shared_ptr<Detail::WorldEntitySlot> slot, Detail::WorldHold &hold);
        // A client's entity, which the server constructed there: known by id, with no lock to take
        // around the Framework's own calls on it.
        void Adopt(NetworkEntity *entity);
        // An entity deleted by another way than Destroy.
        void Forget(NetworkEntity *entity);

        // Destroy in two steps, for a caller with something to do in between. The first answers in
        // one look whether the id resolved, had been destroyed already and waits on a handle, or
        // is unknown, and hands back the slot when nothing holds it; the second tears that one down.
        enum class Doom : uint8_t {
            Unknown,
            Pending,
            Marked
        };
        Doom MarkDoomed(uint64_t id, std::shared_ptr<Detail::WorldEntitySlot> &unheld);
        void TeardownUnheld(std::shared_ptr<Detail::WorldEntitySlot> slot);

        // Around a creation callback: the new entity's lock is held for the creator but not yet
        // counted among its handles, so RequireWorldFree is told another way.
        class Creating final {
          public:
            Creating();
            ~Creating();
            Creating(const Creating &)            = delete;
            Creating &operator=(const Creating &) = delete;
        };

        // While one lives, the calling thread runs nothing it put off and tears nothing down when it
        // lets go of a handle: for a walk made under a lock that what is put off must not run
        // under (a delegation pass holds ReplicaManager3's world). Whoever made one calls Settle
        // once that lock is gone.
        class HoldOff final {
          public:
            HoldOff();
            ~HoldOff();
            HoldOff(const HoldOff &)            = delete;
            HoldOff &operator=(const HoldOff &) = delete;
        };

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
        std::atomic<bool> _registrationClosed {false};
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
