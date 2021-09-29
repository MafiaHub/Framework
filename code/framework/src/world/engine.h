#pragma once

#include "errors.h"

#include <flecs/flecs.h>

namespace Framework::World {
    class Engine {
      private:
        flecs::world *_world = nullptr;

      public:
        EngineError Init();

        EngineError Shutdown();

        void Update(float dt = 0.0f);

        flecs::world *GetWorld() {
            return _world;
        }
    };
} // namespace Framework::World
