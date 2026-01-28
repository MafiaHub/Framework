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

namespace Framework::World::Archetypes {
    class StreamingFactory {
      private:
        inline void SetupDefaults(flecs::entity e, uint64_t guid) {
            // Inherit from prefab for efficient component allocation (LP-3)
            e.is_a(Modules::Base::StreamableEntityPrefab);

            // Configure instance-specific values
            auto &streamable                 = e.ensure<Framework::World::Modules::Base::Streamable>();
            streamable.owner                 = guid;
            streamable.defaultUpdateInterval = CoreModules::GetTickRate() * 1000.0f; // we need ms here
        }

      public:
        inline void SetupClient(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);
        }

        inline void SetupServer(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);
        }
    };
} // namespace Framework::World::Archetypes
