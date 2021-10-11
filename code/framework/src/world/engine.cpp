/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

#include "modules/base.hpp"
#include "modules/network.hpp"

namespace Framework::World {
    EngineError Engine::Init() {
        _world = std::make_unique<flecs::world>();

        // Register a base module
        _world->import<Modules::Base>();
        _world->import<Modules::Network>();

        return EngineError::ENGINE_NONE;
    }

    EngineError Engine::Shutdown() {
        return EngineError::ENGINE_NONE;
    }

    void Engine::Update() {
        _world->progress();
    }
} // namespace Framework::World
