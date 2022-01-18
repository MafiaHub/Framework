/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "engine.h"
#include "errors.h"

#include <flecs/flecs.h>

#include <memory>
#include <string>

namespace Framework::World {
    class ServerEngine: public Engine {
      public:
        EngineError Init(Framework::Networking::NetworkPeer *networkPeer, float tickInterval);

        EngineError Shutdown() override;

        void Update() override;

        flecs::entity CreateEntity(std::string name = "");
        flecs::entity WrapEntity(flecs::entity_t serverID);
        bool IsEntityOwner(flecs::entity e, uint64_t guid);

        void RemoveEntity(flecs::entity e);
    };
} // namespace Framework::World
