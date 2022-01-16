/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// TODO make it work, it's currently not hooked up yet!!!

#include "networking/network_peer.h"
#include "world/modules/base.hpp"

#include "../modules/mod.hpp"

#include <flecs/flecs.h>

namespace Framework::Integrations::Shared::Archetypes {
    class PlayerFactory {
      private:
        flecs::world *_world = nullptr;

        inline flecs::entity CreateCommon(flecs::entity e, uint64_t guid) {

            e.add<World::Modules::Base::Transform>();
            e.add<World::Modules::Base::Frame>();

            auto streamer  = e.get_mut<World::Modules::Base::Streamer>();
            streamer->guid = guid;

            auto streamable   = e.get_mut<World::Modules::Base::Streamable>();
            streamable->owner = guid;

            return e;
        }

      public:
        PlayerFactory(flecs::world *world): _world(world) {}

        inline flecs::entity CreateClient(uint64_t guid, void* actor, flecs::entity_t entityID) {
            auto e = _world->entity(entityID);
            e = CreateCommon(e, guid);

            auto gameActor = e.get_mut<Modules::Mod::GameActor>();
            gameActor->_actor = actor;

            return e;
        }

        inline flecs::entity CreateServer(uint64_t guid) {
            auto e = _world->entity();
            e      = CreateCommon(e, guid);

            auto streamable = e.get_mut<World::Modules::Base::Streamable>();
            World::Modules::Base::SetupDefaultEvents(streamable);

            return e;
        }
    };
} // namespace Framework::Integrations::Shared::Archetypes
