/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "modules/base.hpp"

#include "networking/network_peer.h"

#include <flecs/flecs.h>
#include <memory>

namespace Framework::World {
    class Engine {
      protected:
        flecs::query<Modules::Base::Streamer> _findAllStreamerEntities;
        std::unique_ptr<flecs::world> _world;
        Networking::NetworkPeer *_networkPeer = nullptr;

      public:
        EngineError Init(Networking::NetworkPeer *networkPeer);

        virtual EngineError Shutdown();

        virtual void Update();

        flecs::entity GetEntityByGUID(uint64_t guid);

        flecs::world *GetWorld() {
            return _world.get();
        }
    };
} // namespace Framework::World
