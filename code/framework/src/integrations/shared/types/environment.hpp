/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../messages/weather_update.h"
#include "../modules/mod.hpp"
#include "networking/network_peer.h"
#include "world/modules/base.hpp"

#include <flecs/flecs.h>

namespace Framework::Integrations::Shared::Archetypes {
    class EnvironmentFactory {
      private:
        flecs::world *_world = nullptr;

      public:
        EnvironmentFactory(flecs::world *world): _world(world) {}

        template <typename... Args>
        inline flecs::entity CreateWeather(Args &&...args) {
            auto e = _world->entity().add<World::Modules::Base::Transform>();

            auto weatherData       = e.get_mut<Shared::Modules::Mod::Environment>();
            weatherData->timeHours = 12.0f;

            auto streamable           = e.get_mut<World::Modules::Base::Streamable>();
            streamable->alwaysVisible = true;

            auto weatherEvents       = World::Modules::Base::Streamable::Events {};
            weatherEvents.updateProc = [this](Framework::Networking::NetworkPeer *peer, uint64_t g, flecs::entity &e) {
                auto weather = e.get<Shared::Modules::Mod::Environment>();
                Framework::Integrations::Shared::Messages::WeatherUpdate msg;
                msg.FromParameters(weather->timeHours, false, "");
                peer->Send(msg, g);
                return true;
            };

            streamable->events = weatherEvents;
            return e;
        }
    };
} // namespace Framework::Integrations::Shared::Archetypes
