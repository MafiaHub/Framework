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

#define FW_SEND_COMPONENT_GAME_RPC(rpc, ent, ...)\
    do {\
        auto s = rpc {};\
        s.FromParameters(__VA_ARGS__);\
        s.SetServerID(ent.id());\
        auto net = reinterpret_cast < Framework::Networking::NetworkServer*>(Framework::Networking::NetworkPeer::_networkRef);\
        if (net) {\
            net->SendGameRPC<rpc>(reinterpret_cast<Framework::World::ServerEngine*>(Framework::World::Engine::_worldEngineRef), s);\
        }\
    } while (0)

#define FW_SEND_COMPONENT_RPC(rpc, ...)                                                                                                                                                                                                                                             \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                          \
        auto net = reinterpret_cast<Framework::Networking::NetworkServer *>(Framework::Networking::NetworkPeer::_networkRef);                                                                                                                                                          \
        if (net) {                                                                                                                                                                                                                                                                     \
            net->SendRPC<rpc>(s);                                                                                                                                                   \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

namespace Framework::World {
    class Engine {
      protected:
        flecs::query<Modules::Base::Streamer> _findAllStreamerEntities;
        flecs::query<Modules::Base::Transform, Modules::Base::Streamable> _allStreamableEntities;
        std::unique_ptr<flecs::world> _world;
        Networking::NetworkPeer *_networkPeer = nullptr;

      public:
        EngineError Init(Networking::NetworkPeer *networkPeer);

        virtual EngineError Shutdown();

        virtual void Update();

        flecs::entity GetEntityByGUID(uint64_t guid) const;
        flecs::entity WrapEntity(flecs::entity_t serverID) const;
        static bool IsEntityOwner(flecs::entity e, uint64_t guid);

        flecs::world *GetWorld() {
            return _world.get();
        }

        static inline flecs::world *_worldRef = nullptr;
        static inline Engine *_worldEngineRef = nullptr;
    };
} // namespace Framework::World
