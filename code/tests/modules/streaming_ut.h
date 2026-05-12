/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2024, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "world/server.h"

// Tests covering the ECS modernization work in the world layer:
// - observer-maintained guid cache on the Engine (must handle reassignment)
// - DependsOn relation cleanup when a target entity is destructed
//
// These exercise behavior that the rest of the suite does not touch.

MODULE(streaming, {
    using namespace Framework::World;

    IT("guid -> entity cache stays consistent across Streamer.guid reassignment", {
        ServerEngine engine;
        EQUALS(engine.Init(nullptr, {}), WorldError::WORLD_NONE);
        auto *world = engine.GetWorld();

        const auto e1 = world->entity().set<Modules::Base::Streamer>({});
        e1.try_get_mut<Modules::Base::Streamer>()->guid = 100;
        e1.modified<Modules::Base::Streamer>();

        const auto e2 = world->entity().set<Modules::Base::Streamer>({});
        e2.try_get_mut<Modules::Base::Streamer>()->guid = 200;
        e2.modified<Modules::Base::Streamer>();

        EQUALS(engine.GetEntityByGUID(100).id(), e1.id());
        EQUALS(engine.GetEntityByGUID(200).id(), e2.id());

        // Reassign e1's guid. The OnSet observer should evict the stale
        // forward entry so the old guid no longer resolves to e1.
        e1.try_get_mut<Modules::Base::Streamer>()->guid = 300;
        e1.modified<Modules::Base::Streamer>();

        EQUALS(engine.GetEntityByGUID(100).is_alive(), false);
        EQUALS(engine.GetEntityByGUID(300).id(), e1.id());
        EQUALS(engine.GetEntityByGUID(200).id(), e2.id());

        engine.Shutdown();
    });

    IT("DependsOn pair is removed when the target entity is destructed", {
        flecs::world world;
        world.import<Modules::Base>();

        const auto target = world.entity();
        const auto source = world.entity().add<Modules::Base::DependsOn>(target);

        EQUALS(source.has<Modules::Base::DependsOn>(target), true);

        target.destruct();

        // (OnDeleteTarget, Remove) on the DependsOn relation should have
        // cleaned up the pair on `source`, but the source entity itself must
        // still be alive.
        EQUALS(source.is_alive(), true);
        EQUALS(source.has<Modules::Base::DependsOn>(target), false);
    });
});
