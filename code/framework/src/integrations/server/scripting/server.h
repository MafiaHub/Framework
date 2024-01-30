/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/module.h"
#include "world/server.h"

namespace Framework::Integrations::Scripting {
    class ServerEngine: public Framework::Scripting::Module {
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
