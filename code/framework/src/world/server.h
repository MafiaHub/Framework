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
#include <memory>

namespace Framework::World {
    class ServerEngine: public Engine {
      private:
        flecs::entity _streamEntities;
        float _tickDelay = 7.183f;

      public:
        EngineError Init() override;

        EngineError Shutdown() override;

        void Update() override;

        void SetTickInterval(float ms) {
            _tickDelay = ms;
            ecs_set_interval(_world->get_world(), _streamEntities.id(), ms);
        }

        float GetTickInterval() const {
            return _tickDelay;
        }
    };
} // namespace Framework::World
