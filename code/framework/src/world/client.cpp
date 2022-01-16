/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "client.h"

namespace Framework::World {
    EngineError ClientEngine::Init() {
        const auto status = Engine::Init();

        if (status != EngineError::ENGINE_NONE) {
            return status;
        }

        _world->system<Modules::Base::Transform, Modules::Base::Streamer, Modules::Base::Streamable>("StreamEntities")
                .kind(flecs::PostUpdate)
                .interval(tickInterval)
                .iter([this](flecs::iter it, Modules::Base::Transform *tr, Modules::Base::Streamer *s, Modules::Base::Streamable *rs) {
                    const auto myGUID = 
                    for (size_t i = 0; i < it.count(); i++) {
                        
                    }
                });

        return EngineError::ENGINE_NONE;
    }

    EngineError ClientEngine::Shutdown() {
        return Engine::Shutdown();
    }

    void ClientEngine::Update() {
        Engine::Update();
    }
} // namespace Framework::World
