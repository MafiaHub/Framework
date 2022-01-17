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
#include "networking/network_peer.h"

#include <flecs/flecs.h>
#include <memory>

namespace Framework::World {
    class ServerEngine: public Engine {
      private:
        Framework::Networking::NetworkPeer *_networkPeer = nullptr;

      public:
        EngineError Init(Framework::Networking::NetworkPeer *networkPeer, float tickInterval);

        EngineError Shutdown() override;

        void Update() override;

        void RemoveEntity(flecs::entity e);
    };
} // namespace Framework::World
