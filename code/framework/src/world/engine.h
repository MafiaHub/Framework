/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "modules/base.hpp"

#include "core_modules.h"
#include "networking/network_peer.h"

#include <flecs/flecs.h>
#include <memory>

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Framework::World {
    class Engine {
      private:
        friend class Framework::Scripting::ResourceManager;
        void PurgeAllResourceEntities() const;

      protected:
        // NOTE: _world must be declared BEFORE queries so it's destroyed LAST.
        // Queries reference the world and must be destroyed before the world.
        std::unique_ptr<flecs::world> _world;
        flecs::query<Modules::Base::Streamer> _findAllStreamerEntities;
        flecs::query<Modules::Base::Transform, Modules::Base::Streamable> _allStreamableEntities;
        flecs::query<Modules::Base::RemovedOnResourceReload> _findAllResourceEntities;
        Networking::NetworkPeer *_networkPeer = nullptr;

      public:
        EngineError Init(Networking::NetworkPeer *networkPeer);

        virtual EngineError Shutdown();

        virtual void Update();

        flecs::entity GetEntityByGUID(uint64_t guid) const;
        flecs::entity WrapEntity(flecs::entity_t serverID) const;
        static bool IsEntityOwner(flecs::entity e, uint64_t guid);
        void WakeEntity(flecs::entity e);

        flecs::world *GetWorld() const {
            return _world.get();
        }
    };
} // namespace Framework::World
