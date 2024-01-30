/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <flecs/flecs.h>

#include "world/modules/base.hpp"

#include <string>

namespace Framework::World::Archetypes {
    class PlayerFactory {
      private:
        inline void SetupDefaults(flecs::entity e, uint64_t guid) {
            const auto streamer = e.get_mut<World::Modules::Base::Streamer>();
            streamer->guid      = guid;
        }

      public:
        inline void SetupClient(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);
        }

        inline void SetupServer(flecs::entity e, uint64_t guid, std::string nickname) {
            SetupDefaults(e, guid);

            const auto streamable       = e.get_mut<World::Modules::Base::Streamable>();
            streamable->assignOwnerProc = [](flecs::entity, World::Modules::Base::Streamable &) {
                return true; /* always keep current owner */
            };

            const auto streamer = e.get_mut<World::Modules::Base::Streamer>();
            streamer->nickname  = nickname;
        }
    };
} // namespace Framework::World::Archetypes
