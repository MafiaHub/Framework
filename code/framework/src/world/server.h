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
#include <unordered_set>
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

        // Specialized queries for decomposed visibility system (MP-7)
        // AlwaysVisible is a zero-size tag, added via .with<>() in query builder
        flecs::query<Modules::Base::Transform, Modules::Base::Streamable> _alwaysVisibleEntities;
        flecs::query<Modules::Base::Transform, Modules::Base::Streamable> _normalStreamableEntities;

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

        EngineError Init(Framework::Networking::NetworkPeer *networkPeer, ServerConfig cfg);

        EngineError Shutdown() override;

        void Update() override;

        flecs::entity CreateEntity(const std::string &name = "") const;
        static bool RemoveEntity(flecs::entity e);

        static void SetOwner(flecs::entity e, uint64_t guid);
        flecs::entity GetOwner(flecs::entity e) const;

        // Ownership using Flecs relations
        void SetOwnerRelation(flecs::entity e, flecs::entity owner);
        flecs::entity GetOwnerRelation(flecs::entity e) const;
        static bool IsOwnedBy(flecs::entity e, flecs::entity owner);

        [[maybe_unused]] std::vector<flecs::entity> FindVisibleStreamers(flecs::entity e) const;
        bool IsEntityVisibleToStreamer(const flecs::entity streamerEntity, const flecs::entity e, const Modules::Base::Transform &lhsTr, const Modules::Base::Streamer &streamer, const Modules::Base::Streamable &lhsS, const Modules::Base::Transform &rhsTr,
            const Modules::Base::Streamable &rhsS) const;
    };
} // namespace Framework::World
