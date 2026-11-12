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
        Fixture fixture;
        World &world     = fixture.world;
        const uint64_t a = fixture.Make<Walker>();
        const uint64_t b = fixture.Make<Prop>();

        constexpr int kIterations = 20000;
        const auto started        = std::chrono::steady_clock::now();
        std::thread forward([&] {
            for (int i = 0; i < kIterations; ++i) {
                auto [walker, prop] = world.Write<Walker, Prop>(a, b);
                ++walker->steps;
                ++prop->uses;
            }
        });
        std::thread backward([&] {
            for (int i = 0; i < kIterations; ++i) {
                auto [prop, walker] = world.Write<Prop, Walker>(b, a);
                ++walker->steps;
                ++prop->uses;
            }
        });
        forward.join();
        backward.join();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();

        LESSER(elapsed, 10000);
        auto [walker, prop] = world.Read<Walker, Prop>(a, b);
        EQUALS(walker->steps, 2 * kIterations);
        EQUALS(prop->uses, 2 * kIterations);
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
            EQUALS(fixture.manager->GetEntityByNetworkID(walker) != nullptr, true);
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
