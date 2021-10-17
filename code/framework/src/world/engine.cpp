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
        _world = std::make_unique<flecs::world>();

        // Register a base module
        _world->import<Modules::Base>();

        _findAllStreamerEntities = _world->query_builder<Modules::Base::Streamer>().build();

        return EngineError::ENGINE_NONE;
    }

    EngineError Engine::Shutdown() {
        return EngineError::ENGINE_NONE;
    }

    void Engine::Update() {
        _world->progress();
    }
    
    flecs::entity Engine::GetEntityByGUID(SLNet::RakNetGUID guid) {
        flecs::entity ourEntity;
        _findAllStreamerEntities.iter([&ourEntity, guid](flecs::iter &it, Modules::Base::Streamer *s) {
            for (auto i : it) {
                if (s[i].guid == guid) {
                    ourEntity = it.entity(i);
                    return;
                }
            }
        });

        return ourEntity;
    }
} // namespace Framework::World
