/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

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
