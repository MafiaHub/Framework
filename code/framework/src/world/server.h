/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "engine.h"
#include "errors.h"

#include <flecs/flecs.h>

#include <function2.hpp>
#include <memory>
#include <string>
#include <vector>

#define FW_SEND_SERVER_COMPONENT_GAME_RPC(rpc, ent, ...)                                                                                                                                                                                                                               \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        s.SetServerID(ent.id());                                                                                                                                                                                                                                                       \
        auto __net = reinterpret_cast<Framework::Networking::NetworkServer *>(Framework::CoreModules::GetNetworkPeer());                                                                                                                                                               \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendGameRPC<rpc>(reinterpret_cast<Framework::World::ServerEngine *>(Framework::CoreModules::GetWorldEngine()), s);                                                                                                                                                  \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

#define FW_SEND_SERVER_COMPONENT_GAME_RPC_EXCEPT(rpc, ent, guid, ...)                                                                                                                                                                                                                  \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        s.SetServerID(ent.id());                                                                                                                                                                                                                                                       \
        auto __net = reinterpret_cast<Framework::Networking::NetworkServer *>(Framework::CoreModules::GetNetworkPeer());                                                                                                                                                               \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendGameRPC<rpc>(reinterpret_cast<Framework::World::ServerEngine *>(Framework::CoreModules::GetWorldEngine()), s, SLNet::UNASSIGNED_RAKNET_GUID, guid);                                                                                                             \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

#define FW_SEND_SERVER_COMPONENT_GAME_RPC_TO(rpc, ent, guid, ...)                                                                                                                                                                                                                      \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        s.SetServerID(ent.id());                                                                                                                                                                                                                                                       \
        auto __net = reinterpret_cast<Framework::Networking::NetworkServer *>(Framework::CoreModules::GetNetworkPeer());                                                                                                                                                               \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendGameRPC<rpc>(reinterpret_cast<Framework::World::ServerEngine *>(Framework::CoreModules::GetWorldEngine()), s, guid);                                                                                                                                            \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

namespace Framework::World {
    class ServerEngine: public Engine {
      protected:
        using IsVisibleProc = fu2::function<bool(const flecs::entity streamerEntity, const flecs::entity e, const Modules::Base::Transform &lhsTr, const Modules::Base::Streamer &streamer, const Modules::Base::Streamable &lhsS, const Modules::Base::Transform &rhsTr,
            const Modules::Base::Streamable rhsS) const>;
        IsVisibleProc _isEntityVisible;

      public:
        EngineError Init(Framework::Networking::NetworkPeer *networkPeer, float tickInterval);

        EngineError Shutdown() override;

        void Update() override;

        flecs::entity CreateEntity(const std::string &name = "") const;
        static void RemoveEntity(flecs::entity e);

        static void SetOwner(flecs::entity e, uint64_t guid);
        flecs::entity GetOwner(flecs::entity e) const;
        [[maybe_unused]] std::vector<flecs::entity> FindVisibleStreamers(flecs::entity e) const;
    };
} // namespace Framework::World
