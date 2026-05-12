/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <flecs/distr/flecs.h>

#include "world/modules/base.hpp"

namespace Framework::World::Archetypes {
    class StreamingFactory {
      private:
        inline void SetupDefaults(flecs::entity e, uint64_t guid) {
            e.add<Framework::World::Modules::Base::Transform>();

            auto &streamable                 = e.ensure<Framework::World::Modules::Base::Streamable>();
            streamable.owner                 = guid;
            streamable.defaultUpdateInterval = CoreModules::GetTickRate() * 1000.0f; // we need ms here

            e.add<Framework::World::Modules::Base::TickRateRegulator>();
        }

      public:
        // Streaming-event observers (registered once at engine init via
        // Base::RegisterServerStreamObservers / RegisterClientStreamObservers)
        // translate stream events into network messages. The factories only
        // need to ensure Streamable exists on the entity — no per-entity
        // emitter wiring is required.
        inline void SetupClient(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);
            e.ensure<Framework::World::Modules::Base::Streamable>();
        }

        inline void SetupServer(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);
            e.ensure<Framework::World::Modules::Base::Streamable>();
        }
    };
} // namespace Framework::World::Archetypes
