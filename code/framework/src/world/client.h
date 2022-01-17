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

namespace Framework::World {
    class ClientEngine: public Engine {
      private:
        flecs::entity _streamEntities;
        Networking::NetworkPeer *_peer = nullptr;
      public:
        EngineError Init() override;

        EngineError Shutdown() override;

        void OnConnect(Networking::NetworkPeer *peer, float tickInterval);
        void OnDisconnect();

        void Update() override;

        flecs::entity GetEntityByServerID(flecs::entity_t id);
        flecs::entity GetEntityByGUID(uint64_t id);
    };
} // namespace Framework::World
