/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "engine.h"
#include "errors.h"

#include <flecs/flecs.h>

namespace Framework::World {
    class ClientEngine: public Engine {
      public:
        EngineError Init() override;

        EngineError Shutdown() override;

        void Update() override;
    };
} // namespace Framework::World
