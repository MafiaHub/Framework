/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

// TODO make it work, it's currently not hooked up yet!!!

#include "world/modules/base.hpp"
#include "world/modules/network.hpp"

#include <flecs/flecs.h>

namespace Framework::Integrations::Shared::Archetypes {
    class PlayerFactory {
      private:
        flecs::world *_world = nullptr;

      public:
        PlayerFactory(flecs::world *world): _world(world) {}

        template <typename... Args>
        inline flecs::entity Create(Args &&...args) {
            auto e = _world->entity<Args...>(std::forward<Args>(args)...);

            e.add<Modules::Base::Transform>();
            e.add<Modules::Base::Frame>();
            e.add<Modules::Network::Streamer>();
            auto streamable = e.get_mut<Modules::Network::Streamable>();

            // TODO events
            return e;
        }
    };
} // namespace Framework::Integrations::Shared::Archetypes
