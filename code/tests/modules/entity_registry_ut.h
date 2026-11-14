/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../replication_test_world.h"
#include "networking/network_server.h"
#include "networking/replication/entities/text_label_entity.h"
#include "networking/replication/entity_registry.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"

namespace EntityRegistryTest {
    struct Walker: Framework::Networking::Replication::NetworkEntity {
        static constexpr const char *kTypeName = "Test::EntityRegistry::Walker";
    };
    // Inherits Walker's kTypeName, as a game's client-side entity does.
    struct ClientWalker final: Walker {};
    struct Prop final: Framework::Networking::Replication::NetworkEntity {
        static constexpr const char *kTypeName = "Test::EntityRegistry::Prop";
    };
} // namespace EntityRegistryTest

MODULE(entity_registry, {
    using namespace Framework::Networking::Replication;
    using namespace EntityRegistryTest;

    IT("keeps type ids stable on the wire", {
        static_assert(EntityTypeId<Entities::TextLabelEntity>() == 0x1803d2b3u, "Framework::TextLabel wire id changed");
        static_assert(EntityTypeId<ClientWalker>() == EntityTypeId<Walker>());
        EQUALS(EntityRegistry::TypeId(Entities::TextLabelEntity::kTypeName), 0x1803d2b3u);
        EQUALS(EntityRegistry::TypeId(std::string(Entities::TextLabelEntity::kTypeName)), 0x1803d2b3u);
    });

    IT("gives a type the id its name registers under, and a subclass its parent's", {
        const uint32_t walker = EntityRegistry::Get().Register<Walker>(Walker::kTypeName);
        EQUALS(EntityTypeId<Walker>(), walker);
        EQUALS(EntityTypeId<ClientWalker>(), walker);
        NEQUALS(EntityTypeId<Prop>(), walker);
    });

    IT("resolves typed lookups by type id", {
        Framework::Networking::NetworkServer peer;
        ReplicationManager *manager = peer.GetReplicationManager();
        manager->Init(&peer, true);
        struct Cleanup {
            ReplicationManager *manager;
            ~Cleanup() {
                manager->Clear();
            }
        } cleanup {manager};
        EntityRegistry::Get().Register<Walker>(Walker::kTypeName);
        EntityRegistry::Get().Register<Prop>(Prop::kTypeName);

        Walker *walker = TestWorld::Make<Walker>(manager);
        Prop *prop     = TestWorld::Make<Prop>(manager);
        NEQUALS(walker, nullptr);
        NEQUALS(prop, nullptr);
        EQUALS(walker->GetTypeId(), EntityTypeId<Walker>());
        NEQUALS(prop->GetTypeId(), EntityTypeId<Walker>());
        EQUALS(TestWorld::Find<Walker>(manager, walker->GetNetworkID()), walker);
        EQUALS(TestWorld::Find<Prop>(manager, walker->GetNetworkID()), nullptr);

        int walkers = 0;
        manager->GetWorld().ForEach<Walker>([&walkers](Framework::Networking::Replication::Ref<Walker> &) {
            ++walkers;
        });
        EQUALS(walkers, 1);

        TestWorld::Destroy(manager, walker);
        TestWorld::Destroy(manager, prop);
    });
})
