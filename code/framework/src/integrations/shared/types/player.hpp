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
#include "networking/network_peer.h"

#include <flecs/flecs.h>

namespace Framework::Integrations::Shared::Archetypes {
    class PlayerFactory {
      private:
        flecs::world *_world = nullptr;
        Networking::NetworkPeer *_networkPeer = nullptr;

      public:
        PlayerFactory(flecs::world *world, Networking::NetworkPeer *networkPeer): _world(world), _networkPeer(networkPeer) {}

        inline flecs::entity Create(uint64_t guid) {
            auto e = _world->entity();

            e.add<World::Modules::Base::Transform>();
            e.add<World::Modules::Base::Frame>();

            auto streamer = e.get_mut<World::Modules::Base::Streamer>();
            streamer->guid = guid;

            auto streamable = e.get_mut<World::Modules::Base::Streamable>();
            World::Modules::Base::SetupDefaultEvents(streamable, _networkPeer);

            return e;
        }
    };
} // namespace Framework::Integrations::Shared::Archetypes
