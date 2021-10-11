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
    class EnvironmentFactory {
      private:
        flecs::world *_world = nullptr;

      public:
        EnvironmentFactory(flecs::world *world): _world(world) {}

        template <typename... Args>
        inline flecs::entity CreateWeather(Modules::Network::Streamable::Events &&events, Args &&...args) {
            auto e = _world->entity<Args...>(std::forward<Args>(args)...);

            e.add<Modules::Base::Environment>();
            auto streamable    = e.get_mut<Modules::Network::Streamable>();
            streamable->events = events;
            streamable->alwaysVisible = true;
            return e;
        }
    };
} // namespace Framework::World::Archetypes
