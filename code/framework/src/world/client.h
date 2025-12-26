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

// Forward declarations for RPC handlers
namespace Framework::World::RPC {
    class SetTransform;
    class SetFrame;
} // namespace Framework::World::RPC

namespace Framework::World {
    class ClientEngine: public Engine {
      public:
        using OnEntityDestroyCallback = fu2::function<bool(flecs::entity) const>;

      protected:
        flecs::entity _streamEntities;
        flecs::query<Modules::Base::ServerID> _queryGetEntityByServerID;
        OnEntityDestroyCallback _onEntityDestroyCallback;

      private:
        void InitRPCs(Networking::NetworkPeer *peer);

        // RPC handlers
        void OnSetTransform(SLNet::RakNetGUID guid, RPC::SetTransform *rpc);
        void OnSetFrame(SLNet::RakNetGUID guid, RPC::SetFrame *rpc);

      public:
        EngineError Init();

        EngineError Shutdown() override;

        void OnConnect(Networking::NetworkPeer *peer, float tickInterval);
        void OnDisconnect();

        void Update() override;

        flecs::entity CreateEntity(flecs::entity_t serverID) const;
        flecs::entity GetEntityByServerID(flecs::entity_t id) const;
        static flecs::entity_t GetServerID(flecs::entity entity);
        static void UpdateEntityTransform(flecs::entity entity, Modules::Base::Transform &rhs);

        void SetOnEntityDestroyCallback(OnEntityDestroyCallback cb) {
            _onEntityDestroyCallback = cb;
        }
    };
} // namespace Framework::World
