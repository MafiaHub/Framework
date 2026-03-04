/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "engine.h"

#include <flecs/flecs.h>

#include <function2.hpp>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#define FW_SEND_SERVER_COMPONENT_GAME_RPC(rpc, ent, ...)                                                                                                                                                                                                                               \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        s.SetServerID(ent.id());                                                                                                                                                                                                                                                       \
        auto __net = static_cast<Framework::Networking::NetworkServer *>(Framework::CoreModules::GetNetworkPeer());                                                                                                                                                               \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendGameRPC<rpc>(static_cast<Framework::World::ServerEngine *>(Framework::CoreModules::GetWorldEngine()), s);                                                                                                                                                  \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

#define FW_SEND_SERVER_COMPONENT_GAME_RPC_EXCEPT(rpc, ent, guid, ...)                                                                                                                                                                                                                  \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        s.SetServerID(ent.id());                                                                                                                                                                                                                                                       \
        auto __net = static_cast<Framework::Networking::NetworkServer *>(Framework::CoreModules::GetNetworkPeer());                                                                                                                                                               \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendGameRPC<rpc>(static_cast<Framework::World::ServerEngine *>(Framework::CoreModules::GetWorldEngine()), s, SLNet::UNASSIGNED_RAKNET_GUID, guid);                                                                                                             \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

#define FW_SEND_SERVER_COMPONENT_GAME_RPC_TO(rpc, ent, guid, ...)                                                                                                                                                                                                                      \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        s.SetServerID(ent.id());                                                                                                                                                                                                                                                       \
        auto __net = static_cast<Framework::Networking::NetworkServer *>(Framework::CoreModules::GetNetworkPeer());                                                                                                                                                               \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendGameRPC<rpc>(static_cast<Framework::World::ServerEngine *>(Framework::CoreModules::GetWorldEngine()), s, guid);                                                                                                                                            \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

namespace Framework::World {
    class ServerEngine final : public Engine {
      protected:
        using IsVisibleProc = fu2::function<bool(const flecs::entity streamerEntity, const flecs::entity e, const Modules::Base::Transform &lhsTr, const Modules::Base::Streamer &streamer, const Modules::Base::Streamable &lhsS, const Modules::Base::Transform &rhsTr,
            const Modules::Base::Streamable rhsS) const>;

      private:
        bool IsEntityVisibleToStreamerInternal(const flecs::entity streamerEntity, const flecs::entity e, const Modules::Base::Transform &lhsTr, const Modules::Base::Streamer &streamer, const Modules::Base::Streamable &lhsS, const Modules::Base::Transform &rhsTr,
            const Modules::Base::Streamable &rhsS, std::unordered_set<flecs::entity_t> &visited) const;

      public:
        struct ServerConfig {
            float tickInterval                           = 0.016667f;
            float streamerTickInterval                   = 0.033334f;
            float assignOwnershipTickInterval            = 3.0f;
            float collectRangeExemptEntitiesTickInterval = 0.066668f;
            float removeEntitiesTickInterval             = 0.066668f;
            float tickRegulatorInterval                  = 3.0f;
        };

        [[nodiscard]] WorldError Init(Framework::Networking::NetworkPeer *networkPeer, ServerConfig cfg);

        void Shutdown() override;

        void Update() override;

        flecs::entity CreateEntity(const std::string &name = "") const;
        static bool RemoveEntity(flecs::entity e);

        static void SetOwner(flecs::entity e, uint64_t guid);
        flecs::entity GetOwner(flecs::entity e) const;
        [[maybe_unused]] std::vector<flecs::entity> FindVisibleStreamers(flecs::entity e) const;
        bool IsEntityVisibleToStreamer(const flecs::entity streamerEntity, const flecs::entity e, const Modules::Base::Transform &lhsTr, const Modules::Base::Streamer &streamer, const Modules::Base::Streamable &lhsS, const Modules::Base::Transform &rhsTr,
            const Modules::Base::Streamable &rhsS) const;
    };
} // namespace Framework::World
