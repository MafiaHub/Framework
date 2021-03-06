/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/engine.h"
#include "world/server.h"

namespace Framework::Integrations::Scripting {
    class ServerEngine: public Framework::Scripting::Engine {
      private:
        std::shared_ptr<World::ServerEngine> _world;

      public:
        ServerEngine(std::shared_ptr<World::ServerEngine> world): _world(world) {};

        ~ServerEngine() = default;

        std::shared_ptr<World::ServerEngine> GetWorldEngine() const {
            return _world;
        }
    };
} // namespace Framework::Integrations::Scripting
