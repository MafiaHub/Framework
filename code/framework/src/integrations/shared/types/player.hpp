/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/network_peer.h"
#include "world/modules/base.hpp"

#include "../modules/mod.hpp"

#include "networking/messages/game_sync/entity_update.h"

#include "streaming.hpp"

#include <flecs/flecs.h>

namespace Framework::Integrations::Shared::Archetypes {
    class PlayerFactory {
      private:
        inline void SetupDefaults(flecs::entity e, uint64_t guid) {
            auto streamer  = e.get_mut<World::Modules::Base::Streamer>();
            streamer->guid = guid;
        }

      public:
        inline void SetupClient(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);
        }

        inline void SetupServer(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);
        }
    };
} // namespace Framework::Integrations::Shared::Archetypes
