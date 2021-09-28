#include "engine.h"

#include "modules/base.hpp"

namespace Framework::World {
    EngineError Engine::Init() {
        _world = new flecs::world();
        
        // Register a base module
        _world->import<Modules::Base>();

        return EngineError::ENGINE_NONE;
    }

    EngineError Engine::Shutdown() {
        delete _world;
        _world = nullptr;
        return EngineError::ENGINE_NONE;
    }

    void Engine::Update(float dt) {
        _world->progress(dt);
    }
} // namespace Framework::World
