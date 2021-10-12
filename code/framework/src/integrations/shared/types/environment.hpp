/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "../../server/networking/engine.h"
#include "../messages/weather_update.h"
#include "world/modules/base.hpp"
#include "world/modules/network.hpp"

#include <flecs/flecs.h>

namespace Framework::Integrations::Shared::Archetypes {
    class EnvironmentFactory {
      private:
        flecs::world *_world = nullptr;
        Integrations::Server::Networking::Engine *_networkingEngine;

      public:
        EnvironmentFactory(flecs::world *world, Integrations::Server::Networking::Engine *networkingEngine): _world(world), _networkingEngine(networkingEngine) {}

        template <typename... Args>
        inline flecs::entity CreateWeather(Args &&...args) {
            auto e = _world->entity<Args...>(std::forward<Args>(args)...);

            e.add<World::Modules::Base::Environment>();
            auto streamable           = e.get_mut<World::Modules::Network::Streamable>();
            streamable->alwaysVisible = true;

            auto weatherEvents       = World::Modules::Network::Streamable::Events {};
            weatherEvents.updateProc = [this](SLNet::RakNetGUID g, flecs::entity &e) {
                auto weather = e.get<World::Modules::Base::Environment>();
                Framework::Integrations::Shared::Messages::WeatherUpdate msg;
                msg.FromParameters(weather->timeHours, false, "");
                _networkingEngine->GetNetworkServer()->Send(msg, g);
                return true;
            };

            streamable->events = weatherEvents;
            return e;
        }
    };
} // namespace Framework::Integrations::Shared::Archetypes
