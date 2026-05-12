/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "engine.h"

#include <flecs/distr/flecs.h>

#include <function2.hpp>
#include <unordered_map>

#define FW_SEND_CLIENT_COMPONENT_GAME_RPC(rpc, ent, ...)                                                                                                                                                                                                                               \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        s.SetServerID(ent.id());                                                                                                                                                                                                                                                       \
        auto __net = static_cast<Framework::Networking::NetworkClient *>(Framework::CoreModules::GetNetworkPeer());                                                                                                                                                               \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendGameRPC<rpc>(s);                                                                                                                                                                                                                                                \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

#define FW_SEND_CLIENT_COMPONENT_GAME_RPC_TO(rpc, ent, guid, ...)                                                                                                                                                                                                                      \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        s.SetServerID(ent.id());                                                                                                                                                                                                                                                       \
        auto __net = static_cast<Framework::Networking::NetworkClient *>(Framework::CoreModules::GetNetworkPeer());                                                                                                                                                               \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendGameRPC<rpc>(s, guid);                                                                                                                                                                                                                                          \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

namespace Framework::World {
    class ClientEngine final : public Engine {
      public:
        using OnEntityDestroyCallback = fu2::function<bool(flecs::entity) const>;

      protected:
        flecs::entity _streamEntities;
        // Cache of ServerID.id -> entity id, maintained by observers on ServerID.
        std::unordered_map<flecs::entity_t, flecs::entity_t> _serverIDToEntity;
        OnEntityDestroyCallback _onEntityDestroyCallback;

      private:
        void InitRPCs(Networking::NetworkPeer *peer) const;

      public:
        [[nodiscard]] WorldError Init();

        void Shutdown() override;

        void OnConnect(Networking::NetworkPeer *peer, float tickInterval);
        void OnDisconnect();

        void Update() override;

        flecs::entity CreateEntity(flecs::entity_t serverID) const;
        flecs::entity GetEntityByServerID(flecs::entity_t id) const;
        static flecs::entity_t GetServerID(flecs::entity entity);
        static void UpdateEntityTransform(flecs::entity entity, const Modules::Base::Transform &rhs);

        void SetOnEntityDestroyCallback(OnEntityDestroyCallback cb) {
            _onEntityDestroyCallback = cb;
        }
    };
} // namespace Framework::World
