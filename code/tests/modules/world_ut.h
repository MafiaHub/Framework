/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_server.h"
#include "networking/replication/entity_registry.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "networking/replication/world.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace WorldTest {
    struct Walker final: Framework::Networking::Replication::NetworkEntity {
        static constexpr const char *kTypeName = "Test::World::Walker";
        int steps                              = 0;
    };
    struct Prop final: Framework::Networking::Replication::NetworkEntity {
        static constexpr const char *kTypeName = "Test::World::Prop";
        int uses                               = 0;
    };
    // Mod state that is not an entity.
    struct Registry final {
        int records = 0;
    };
    struct Unregistered final {};

    // A server-side manager with no network behind it, emptied when the test leaves.
    struct Fixture final {
        Framework::Networking::NetworkServer peer;
        Framework::Networking::Replication::ReplicationManager *manager = peer.GetReplicationManager();
        Framework::Networking::Replication::World &world                = manager->GetWorld();

        Fixture() {
            manager->Init(&peer, true);
            Framework::Networking::Replication::EntityRegistry::Get().Register<Walker>(Walker::kTypeName);
            Framework::Networking::Replication::EntityRegistry::Get().Register<Prop>(Prop::kTypeName);
        }
        ~Fixture() {
            manager->Clear();
        }

        template <typename T>
        uint64_t Make() {
            return world.Create<T>()->GetNetworkID();
        }
    };
} // namespace WorldTest

MODULE(world, {
    using namespace Framework::Networking::Replication;
    using namespace WorldTest;

    IT("gives an empty handle for an id that does not resolve, and for the wrong type", {
        Fixture fixture;
        World &world          = fixture.world;
        const uint64_t walker = fixture.Make<Walker>();

        EQUALS(static_cast<bool>(world.Read<Walker>(walker + 1000)), false);
        EQUALS(static_cast<bool>(world.Write<Walker>(walker + 1000)), false);
        EQUALS(static_cast<bool>(world.Read<Prop>(walker)), false);
        EQUALS(static_cast<bool>(world.Write<Prop>(walker)), false);
        EQUALS(static_cast<bool>(world.Read<Walker>(walker)), true);
        EQUALS(World::HeldByThisThread(), size_t {0});
    });

    IT("reads and writes an entity through a handle", {
        Fixture fixture;
        World &world          = fixture.world;
        const uint64_t walker = fixture.Make<Walker>();
        {
            Ref<Walker> handle = world.Write<Walker>(walker);
            EQUALS(static_cast<bool>(handle), true);
            EQUALS(handle->GetNetworkID(), walker);
            handle->steps    = 7;
            (*handle).steps += 1;
        }
        ConstRef<Walker> handle = world.Read<Walker>(walker);
        EQUALS(static_cast<bool>(handle), true);
        EQUALS(handle->steps, 8);
        EQUALS((*handle).steps, 8);
    });

    IT("takes several entities at once whatever order they are asked in", {
        // On the heap and left there if the threads never finish: a deadlock then fails the test
        // instead of hanging it.
        auto *fixture    = new Fixture;
        auto *finished   = new std::atomic<int>(0);
        World &world     = fixture->world;
        const uint64_t a = fixture->Make<Walker>();
        const uint64_t b = fixture->Make<Prop>();

        constexpr int kIterations = 20000;
        std::thread forward([&world, finished, a, b] {
            for (int i = 0; i < kIterations; ++i) {
                auto [walker, prop] = world.Write<Walker, Prop>(a, b);
                ++walker->steps;
                ++prop->uses;
            }
            ++*finished;
        });
        std::thread backward([&world, finished, a, b] {
            for (int i = 0; i < kIterations; ++i) {
                auto [prop, walker] = world.Write<Prop, Walker>(b, a);
                ++walker->steps;
                ++prop->uses;
            }
            ++*finished;
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (*finished != 2 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const bool done = *finished == 2;
        if (done) {
            forward.join();
            backward.join();
        }
        else {
            forward.detach();
            backward.detach();
        }
        EQUALS(done, true);
        {
            auto [walker, prop] = world.Read<Walker, Prop>(a, b);
            EQUALS(walker->steps, 2 * kIterations);
            EQUALS(prop->uses, 2 * kIterations);
        }
        delete finished;
        delete fixture;
    });

    IT("fills the first handle and leaves the second empty when one id is asked for twice", {
        Fixture fixture;
        World &world          = fixture.world;
        const uint64_t walker = fixture.Make<Walker>();
        {
            auto [first, second] = world.Write<Walker, Walker>(walker, walker);
            EQUALS(static_cast<bool>(first), true);
            EQUALS(static_cast<bool>(second), false);
            EQUALS(World::HeldByThisThread(), size_t {1});
        }
        auto [first, second] = world.Read<Walker, Walker>(walker, walker);
        EQUALS(static_cast<bool>(first), true);
        EQUALS(static_cast<bool>(second), false);
    });

    IT("keeps an entity alive for a handle held across its destruction, and stops resolving its id at once", {
        Fixture fixture;
        World &world          = fixture.world;
        const uint64_t walker = fixture.Make<Walker>();
        int tornDown          = 0;
        fixture.manager->SetOnEntityDestroyed([&tornDown](uint64_t) {
            ++tornDown;
        });
        {
            Ref<Walker> handle = world.Write<Walker>(walker);
            handle->steps      = 3;
            EQUALS(world.Destroy(walker), true);
            EQUALS(world.Destroy(walker), false);

            // Asked from another thread: this one holds a handle.
            bool resolves = true;
            std::thread other([&] {
                resolves = static_cast<bool>(world.Read<Walker>(walker));
            });
            other.join();
            EQUALS(resolves, false);

            EQUALS(tornDown, 0);
            EQUALS(handle->steps, 3);
        }
        EQUALS(tornDown, 1);
        EQUALS(fixture.manager->GetEntityByNetworkID(walker) == nullptr, true);
        EQUALS(static_cast<bool>(world.Read<Walker>(walker)), false);

        // Nothing holds this one, so it goes at once.
        const uint64_t prop = fixture.Make<Prop>();
        EQUALS(world.Destroy(prop), true);
        EQUALS(tornDown, 2);
        EQUALS(fixture.manager->GetEntityByNetworkID(prop) == nullptr, true);
    });

    IT("hides an entity whose destruction waits on a handle from the raw lookups, and ignores a second destroy", {
        Fixture fixture;
        World &world                   = fixture.world;
        ReplicationManager *manager    = fixture.manager;
        const uint64_t walker          = fixture.Make<Walker>();
        const uint64_t other           = fixture.Make<Walker>();
        const MafiaNet::PeerGuid guid  = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(77));
        const MafiaNet::PeerGuid guid2 = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(78));
        manager->SetViewer(guid, manager->GetEntityByNetworkID(walker));
        manager->SetViewer(guid2, manager->GetEntityByNetworkID(other));
        int tornDown = 0;
        manager->SetOnEntityDestroyed([&tornDown](uint64_t) {
            ++tornDown;
        });
        const auto countEntities = [manager] {
            int count = 0;
            manager->ForEachEntity([&count](NetworkEntity *) {
                ++count;
            });
            return count;
        };
        const auto countViewers = [manager] {
            int count = 0;
            manager->ForEachViewer([&count](MafiaNet::PeerGuid, NetworkEntity *) {
                ++count;
            });
            return count;
        };
        const auto countAvatars = [manager] {
            int count = 0;
            manager->ForEachAvatar([&count](MafiaNet::PeerGuid, NetworkEntity *) {
                ++count;
            });
            return count;
        };
        EQUALS(countEntities(), 2);
        EQUALS(countViewers(), 2);
        {
            Ref<Walker> handle = world.Write<Walker>(walker);
            NetworkEntity *raw = &*handle;
            EQUALS(world.Destroy(walker), true);

            EQUALS(manager->GetEntityByNetworkID(walker) == nullptr, true);
            EQUALS(manager->GetViewer(guid) == nullptr, true);
            EQUALS(manager->GetViewer(guid2) != nullptr, true);
            EQUALS(countEntities(), 1);
            EQUALS(countViewers(), 1);
            EQUALS(countAvatars(), 1);

            manager->DestroyEntity(raw);
            EQUALS(tornDown, 0);
            EQUALS(handle->GetNetworkID(), walker);
        }
        EQUALS(tornDown, 1);
        EQUALS(countEntities(), 1);
        EQUALS(manager->GetEntityByNetworkID(other) != nullptr, true);
    });

    IT("tears an entity down only once the thread holds nothing", {
        Fixture fixture;
        World &world         = fixture.world;
        const uint64_t a     = fixture.Make<Walker>();
        const uint64_t b     = fixture.Make<Prop>();
        const uint64_t c     = fixture.Make<Prop>();
        int tornDown         = 0;
        size_t heldAtMost    = 0;
        bool reachedTheWorld = false;
        // What a script-facing callback may do: ask the world for something.
        fixture.manager->SetOnEntityDestroyed([&](uint64_t) {
            ++tornDown;
            heldAtMost      = std::max(heldAtMost, World::HeldByThisThread());
            reachedTheWorld = static_cast<bool>(world.Read<Prop>(c));
        });
        {
            auto [walker, prop] = world.Write<Walker, Prop>(a, b);
            EQUALS(world.Destroy(a), true);
            {
                const Ref<Walker> released = std::move(walker);
            }
            EQUALS(World::HeldByThisThread(), size_t {1});
            EQUALS(tornDown, 0);
        }
        EQUALS(tornDown, 1);
        EQUALS(heldAtMost, size_t {0});
        EQUALS(reachedTheWorld, true);

        // Nothing holds b, but the thread destroying it holds something else.
        {
            const ConstRef<Prop> unrelated = world.Read<Prop>(c);
            EQUALS(world.Destroy(b), true);
            EQUALS(tornDown, 1);
            EQUALS(fixture.manager->GetEntityByNetworkID(b) == nullptr, true);
        }
        EQUALS(tornDown, 2);
        EQUALS(heldAtMost, size_t {0});
    });

    IT("hands a created entity over already held, before any other thread can take it", {
        Fixture fixture;
        World &world = fixture.world;
        {
            const Ref<Walker> created = world.Create<Walker>();
            EQUALS(static_cast<bool>(created), true);
            EQUALS(World::HeldByThisThread(), size_t {1});
            created->steps = 1;
        }
        EQUALS(World::HeldByThisThread(), size_t {0});

        // The earliest another thread can hear of the entity: it cannot take it from its creator.
        bool destroyedByOther = true;
        fixture.manager->SetOnEntityCreated([&](uint64_t id) {
            std::thread other([&, id] {
                destroyedByOther = world.Destroy(id);
            });
            other.join();
        });
        {
            const Ref<Walker> created = world.Create<Walker>();
            EQUALS(destroyedByOther, false);
            EQUALS(static_cast<bool>(created), true);
            created->steps = 1;
        }
        fixture.manager->SetOnEntityCreated({});

        // Another thread looks for a walker its creator has not finished with.
        std::atomic<bool> stop {false};
        std::atomic<int> sawUnfinished {0};
        std::thread watcher([&] {
            while (!stop) {
                world.ForEach<Walker>([&](Ref<Walker> &walker) {
                    if (walker->steps == 0) {
                        ++sawUnfinished;
                    }
                });
            }
        });
        for (int i = 0; i < 3000; ++i) {
            const Ref<Walker> walker = world.Create<Walker>();
            walker->steps            = 1;
        }
        stop = true;
        watcher.join();
        EQUALS(sawUnfinished.load(), 0);
    });

    IT("gives an empty handle to a thread that waited for an entity destroyed meanwhile", {
        Fixture fixture;
        World &world          = fixture.world;
        const uint64_t walker = fixture.Make<Walker>();
        std::atomic<bool> asking {false};
        bool resolved = true;
        std::thread waiter;
        {
            const Ref<Walker> handle = world.Write<Walker>(walker);
            waiter                   = std::thread([&] {
                asking   = true;
                resolved = static_cast<bool>(world.Write<Walker>(walker));
            });
            while (!asking) {
                std::this_thread::yield();
            }
            // Long enough for the waiter to be waiting on the entity's lock.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            EQUALS(world.Destroy(walker), true);
        }
        waiter.join();
        EQUALS(resolved, false);
        EQUALS(fixture.manager->GetEntityByNetworkID(walker) == nullptr, true);
    });

    IT("visits every entity of a type one lock at a time", {
        Fixture fixture;
        World &world = fixture.world;
        fixture.Make<Walker>();
        fixture.Make<Prop>();
        fixture.Make<Walker>();
        fixture.Make<Walker>();

        int visited      = 0;
        size_t mostHeld  = 0;
        size_t leastHeld = 100;
        world.ForEach<Walker>([&](Ref<Walker> &walker) {
            ++visited;
            walker->steps = 5;
            mostHeld      = std::max(mostHeld, World::HeldByThisThread());
            leastHeld     = std::min(leastHeld, World::HeldByThisThread());
        });
        EQUALS(visited, 3);
        EQUALS(mostHeld, size_t {1});
        EQUALS(leastHeld, size_t {1});
        EQUALS(World::HeldByThisThread(), size_t {0});

        int stepped = 0;
        world.ForEach<Walker>([&](Ref<Walker> &walker) {
            stepped += walker->steps;
        });
        EQUALS(stepped, 15);
    });

    IT("reaches registered state alone and together with an entity", {
        Fixture fixture;
        World &world = fixture.world;
        world.RegisterState(std::make_unique<Registry>());
        const uint64_t walker = fixture.Make<Walker>();

        EQUALS(static_cast<bool>(world.Read<Unregistered>()), false);
        {
            Ref<Registry> registry = world.Write<Registry>();
            EQUALS(static_cast<bool>(registry), true);
            registry->records = 4;
        }
        {
            ConstRef<Registry> registry = world.Read<Registry>();
            EQUALS(static_cast<bool>(registry), true);
            EQUALS(registry->records, 4);
        }
        {
            auto [handle, registry] = world.Write<Walker, Registry>(walker);
            EQUALS(static_cast<bool>(handle), true);
            EQUALS(static_cast<bool>(registry), true);
            handle->steps = ++registry->records;
        }
        auto [registry, handle] = world.Read<Registry, Walker>(walker);
        EQUALS(registry->records, 5);
        EQUALS(handle->steps, 5);
    });

    IT("finishes one teardown before starting the next when its callback takes a handle", {
        Fixture fixture;
        World &world     = fixture.world;
        const uint64_t a = fixture.Make<Walker>();
        const uint64_t b = fixture.Make<Walker>();
        const uint64_t c = fixture.Make<Prop>();
        int tornDown     = 0;
        int inside       = 0;
        int insideAtMost = 0;
        fixture.manager->SetOnEntityDestroyed([&](uint64_t) {
            ++tornDown;
            insideAtMost = std::max(insideAtMost, ++inside);
            {
                // Releasing this must leave the entity still queued to the loop that called here.
                const Ref<Prop> other = world.Write<Prop>(c);
                other->uses++;
            }
            --inside;
        });
        {
            auto [first, second] = world.Write<Walker, Walker>(a, b);
            EQUALS(world.Destroy(a), true);
            EQUALS(world.Destroy(b), true);
            EQUALS(tornDown, 0);
        }
        EQUALS(tornDown, 2);
        EQUALS(insideAtMost, 1);
        EQUALS(world.Read<Prop>(c)->uses, 2);
    });

    IT("refuses to destroy an entity from inside its creation callback", {
        Fixture fixture;
        World &world   = fixture.world;
        bool destroyed = true;
        int tornDown   = 0;
        fixture.manager->SetOnEntityCreated([&](uint64_t id) {
            destroyed = world.Destroy(id);
        });
        fixture.manager->SetOnEntityDestroyed([&](uint64_t) {
            ++tornDown;
        });
        uint64_t id = 0;
        {
            const Ref<Walker> created = world.Create<Walker>();
            EQUALS(static_cast<bool>(created), true);
            created->steps = 3;
            id             = created->GetNetworkID();
        }
        fixture.manager->SetOnEntityCreated({});
        EQUALS(destroyed, false);
        EQUALS(tornDown, 0);
        EQUALS(world.Read<Walker>(id)->steps, 3);
    });

    IT("reaches an entity of any type through the base, and creates one by type id", {
        Fixture fixture;
        World &world          = fixture.world;
        const uint64_t walker = fixture.Make<Walker>();
        const uint64_t prop   = fixture.Make<Prop>();
        {
            const Ref<Framework::Networking::Replication::NetworkEntity> any = world.Write<Framework::Networking::Replication::NetworkEntity>(prop);
            EQUALS(static_cast<bool>(any), true);
            EQUALS(any->GetNetworkID(), prop);
        }
        EQUALS(static_cast<bool>(world.Read<Framework::Networking::Replication::NetworkEntity>(walker + prop + 1)), false);
        {
            auto [base, typed] = world.Read<Framework::Networking::Replication::NetworkEntity, Prop>(walker, prop);
            EQUALS(static_cast<bool>(base), true);
            EQUALS(static_cast<bool>(typed), true);
        }
        int visited = 0;
        world.ForEach<Framework::Networking::Replication::NetworkEntity>([&](Ref<Framework::Networking::Replication::NetworkEntity> &) {
            ++visited;
        });
        EQUALS(visited, 2);

        uint64_t made = 0;
        {
            const Ref<Framework::Networking::Replication::NetworkEntity> created = world.Create(Framework::Networking::Replication::EntityTypeId<Prop>());
            EQUALS(static_cast<bool>(created), true);
            made = created->GetNetworkID();
        }
        EQUALS(static_cast<bool>(world.Read<Prop>(made)), true);
        EQUALS(static_cast<bool>(world.Create(0x12345678u)), false);
    });

    IT("tells a state-change handler once the writer has let go of the entity", {
        using Framework::Networking::Replication::StateChange;
        using Framework::Networking::Replication::StateValue;
        Fixture fixture;
        World &world            = fixture.world;
        const uint64_t walker   = fixture.Make<Walker>();
        int calls               = 0;
        size_t heldInHandler    = 99;
        uint64_t namedEntity    = 0;
        bool handedThePointer   = true;
        bool reachedTheEntity   = false;
        const auto subscription = fixture.manager->AddStateChangeHandler({}, [&](const StateChange &change) {
            ++calls;
            heldInHandler    = World::HeldByThisThread();
            namedEntity      = change.networkId;
            handedThePointer = change.entity != nullptr;
            // What a script's handler does: read the entity it was told about.
            reachedTheEntity = static_cast<bool>(world.Read<Walker>(change.networkId));
        });
        {
            const Ref<Walker> held = world.Write<Walker>(walker);
            StateValue value;
            value.type   = StateValue::Type::Number;
            value.number = 4.0;
            held->state.Set("fuel", value);
            EQUALS(calls, 0);
        }
        fixture.manager->RemoveStateChangeHandler(subscription);
        EQUALS(calls, 1);
        EQUALS(heldInHandler, size_t {0});
        EQUALS(namedEntity, walker);
        EQUALS(handedThePointer, false);
        EQUALS(reachedTheEntity, true);
    });

    IT("keeps the viewer map by id, and takes the flag off a replaced viewer", {
        Fixture fixture;
        World &world                  = fixture.world;
        ReplicationManager *manager   = fixture.manager;
        const uint64_t first          = fixture.Make<Walker>();
        const uint64_t second         = fixture.Make<Walker>();
        const uint64_t third          = fixture.Make<Walker>();
        const MafiaNet::PeerGuid guid = MafiaNet::ToPeerGuid(MafiaNet::RakNetGUID(81));
        EQUALS(manager->GetViewerId(guid), uint64_t {0});

        manager->SetViewer(guid, first);
        EQUALS(manager->GetViewerId(guid), first);
        EQUALS(world.Read<Walker>(first)->streaming.isViewer, true);

        // By id, both entities are taken: the replaced one loses the flag at once.
        manager->SetViewer(guid, second);
        EQUALS(manager->GetViewerId(guid), second);
        EQUALS(world.Read<Walker>(first)->streaming.isViewer, false);
        EQUALS(world.Read<Walker>(second)->streaming.isViewer, true);

        // By handle, only the new one is held: the map answers at once, the old flag goes with the tick.
        {
            const Ref<Walker> held = world.Write<Walker>(third);
            manager->SetViewer(guid, held);
            EQUALS(manager->GetViewerId(guid), third);
            EQUALS(held->streaming.isViewer, true);
        }
        manager->RebuildInterest();
        EQUALS(world.Read<Walker>(second)->streaming.isViewer, false);
        EQUALS(manager->GetViewerIds().size(), size_t {1});

        manager->ClearViewer(guid);
        EQUALS(manager->GetViewerId(guid), uint64_t {0});
        EQUALS(world.Read<Walker>(third)->streaming.isViewer, false);

        // A viewer that is destroyed leaves the map.
        manager->SetViewer(guid, first);
        EQUALS(world.Destroy(first), true);
        EQUALS(manager->GetViewerId(guid), uint64_t {0});
    });

    IT("counts handles held by the thread", {
        Fixture fixture;
        World &world     = fixture.world;
        const uint64_t a = fixture.Make<Walker>();
        const uint64_t b = fixture.Make<Prop>();

        EQUALS(World::HeldByThisThread(), size_t {0});
        {
            auto [walker, prop] = world.Write<Walker, Prop>(a, b);
            EQUALS(World::HeldByThisThread(), size_t {2});
        }
        EQUALS(World::HeldByThisThread(), size_t {0});
    });
});
