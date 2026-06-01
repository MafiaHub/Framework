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
#include "networking/rpc/rpc.h"

#include <flecs/distr/flecs.h>
#include <memory>

#include "core_modules.h"

// Construct an RPC payload from parameters and broadcast it to every connected system. An entity is
// targeted simply by serializing its NetworkID into the payload; there is no separate game-RPC path.
#define FW_BROADCAST_RPC(rpc, ...)                                                                                                                                                                                                                                                      \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        auto __net = Framework::CoreModules::GetNetworkPeer();                                                                                                                                                                                                                          \
        if (__net) {                                                                                                                                                                                                                                                                   \
            Framework::Networking::RPC::Broadcast(__net->GetRPC(), s);                                                                                                                                                                                                                  \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

// Construct an RPC payload from parameters and send it to a single system.
#define FW_SEND_RPC_TO(rpc, guid, ...)                                                                                                                                                                                                                                                 \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        auto __net = Framework::CoreModules::GetNetworkPeer();                                                                                                                                                                                                                          \
        if (__net) {                                                                                                                                                                                                                                                                   \
            Framework::Networking::RPC::SendTo(__net->GetRPC(), s, guid);                                                                                                                                                                                                               \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Framework::World {
    namespace Replication = Framework::Networking::Replication;

    // The world engine is now a thin native facade over MafiaNet's ReplicaManager3
    // (Replication::ReplicationManager), which owns all networked entities (Replication::NetworkEntity).
    // The flecs world it keeps is used ONLY by the scripting resource layer for its resource tree,
    // not for any networked/entity-streaming purpose (that machinery was removed in the native
    // migration).
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

        // Native world access.
        Replication::ReplicationManager *GetReplication() const {
            return _networkPeer ? _networkPeer->GetReplicationManager() : nullptr;
        }
        Replication::NetworkEntity *GetEntityByNetworkID(MafiaNet::NetworkID networkId) const;

        static bool IsEntityOwner(Replication::NetworkEntity *entity, uint64_t guid) {
            return entity && entity->ownerGUID == guid;
        }

        // Resource-layer flecs world (scripting only).
        flecs::world *GetWorld() const {
            return _world.get();
        }
    };
} // namespace Framework::World
