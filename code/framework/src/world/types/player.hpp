/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../modules/base.hpp"
#include "../modules/network.hpp"

#include <flecs/flecs.h>

namespace Framework::World::Archetypes {
    class PlayerFactory {
      private:
        flecs::world *_world = nullptr;
        Modules::Network::Streamable::Events _events;

      public:
        PlayerFactory(flecs::world *world, Modules::Network::Streamable::Events &&events): _world(world), _events(events) {}

        template <typename... Args>
        inline flecs::entity Create(Args &&...args) {
            auto e = _world->entity<Args...>(std::forward<Args>(args)...);

            e.add<Modules::Base::Transform>();
            e.add<Modules::Base::Frame>();
            e.add<Modules::Network::Streamer>();
            auto streamable = e.get_mut<Modules::Network::Streamable>();
            streamable->events = _events;
            return e;
        }
    };
} // namespace Framework::World::Archetypes
