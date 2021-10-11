/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <flecs/flecs.h>
#include <memory>

namespace Framework::World {
    class Engine {
      private:
        std::unique_ptr<flecs::world> _world;

      public:
        EngineError Init();

        EngineError Shutdown();

        void Update();

        flecs::world *GetWorld() {
            return _world.get();
        }
    };
} // namespace Framework::World
