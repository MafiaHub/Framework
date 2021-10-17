/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "modules/base.hpp"

#include <flecs/flecs.h>
#include <memory>
#include <slikenet/types.h>

namespace Framework::World {
    class Engine {
      protected:
        flecs::query<Modules::Base::Streamer> _findAllStreamerEntities;
        std::unique_ptr<flecs::world> _world;

      public:
        virtual EngineError Init();

        virtual EngineError Shutdown();

        virtual void Update();

        flecs::entity GetEntityByGUID(SLNet::RakNetGUID guid);

        flecs::world *GetWorld() {
            return _world.get();
        }
    };
} // namespace Framework::World
