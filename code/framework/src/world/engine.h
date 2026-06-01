/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <utils/lifecycle.h>

#include "networking/network_peer.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"

#include <flecs/distr/flecs.h>
#include <memory>

#include "core_modules.h"

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Framework::World {
    namespace Replication = Framework::Networking::Replication;

    // Facade over the ReplicationManager, which owns the networked entities. The flecs world here
    // backs the scripting resource tree only.
    class Engine : public Lifecycle {
      private:
        friend class Framework::Scripting::ResourceManager;

      protected:
        std::unique_ptr<flecs::world> _world; // resource tree only
        Networking::NetworkPeer *_networkPeer = nullptr;

      public:
        [[nodiscard]] WorldError Init(Networking::NetworkPeer *networkPeer);

        void Shutdown() override;

        void Update() override;

        Replication::ReplicationManager *GetReplication() const {
            return _networkPeer ? _networkPeer->GetReplicationManager() : nullptr;
        }
        Replication::NetworkEntity *GetEntityByNetworkID(MafiaNet::NetworkID networkId) const;

        static bool IsEntityOwner(Replication::NetworkEntity *entity, uint64_t guid) {
            return entity && entity->ownerGUID == guid;
        }

        // flecs world backing the scripting resource tree.
        flecs::world *GetWorld() const {
            return _world.get();
        }
    };
} // namespace Framework::World
