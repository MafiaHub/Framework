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

#include "networking/messages/game_sync/entity_update.h"

#include <flecs/flecs.h>

namespace Framework::Integrations::Shared::Archetypes {
    class PlayerFactory {
      private:
        inline flecs::entity SetupDefaults(flecs::entity e, uint64_t guid) {
            e.add<World::Modules::Base::Transform>();
            e.add<World::Modules::Base::Frame>();

            auto streamer  = e.get_mut<World::Modules::Base::Streamer>();
            streamer->guid = guid;

            auto streamable   = e.get_mut<World::Modules::Base::Streamable>();
            streamable->owner = guid;

            return e;
        }

      public:
        inline flecs::entity SetupClient(flecs::entity e, uint64_t guid) {
            e = SetupDefaults(e, guid);

            auto streamable = e.get_mut<World::Modules::Base::Streamable>();
            World::Modules::Base::SetupDefaultClientEvents(streamable);

            return e;
        }

        inline flecs::entity SetupServer(flecs::entity e, uint64_t guid) {
            e = SetupDefaults(e, guid);

            auto streamable = e.get_mut<World::Modules::Base::Streamable>();
            World::Modules::Base::SetupDefaultEvents(streamable);

            return e;
        }
    };
} // namespace Framework::Integrations::Shared::Archetypes
