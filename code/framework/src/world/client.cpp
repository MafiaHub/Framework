/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
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

        return EngineError::ENGINE_NONE;
    }

    EngineError ClientEngine::Shutdown() {
        return Engine::Shutdown();
    }

    void ClientEngine::Update() {
        Engine::Update();
    }
} // namespace Framework::World
