/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "engine.h"

#include <cstdint>
#include <string>

// Broadcast an RPC payload from the server to everyone except one system (typically the originating
// client). Entity identity, if any, is carried inside the payload (a NetworkID field) — there is no
// separate game-RPC concept. For plain broadcast or targeted sends use FW_BROADCAST_RPC /
// FW_SEND_RPC_TO from engine.h.
#define FW_SERVER_BROADCAST_RPC_EXCEPT(rpc, guid, ...)                                                                                                                                                                                                                                 \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        auto __net = static_cast<Framework::Networking::NetworkServer *>(Framework::CoreModules::GetNetworkPeer());                                                                                                                                                                     \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->BroadcastRPCExcept(s, guid);                                                                                                                                                                                                                                        \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

namespace Framework::World {
    class ServerEngine final : public Engine {
      public:
        struct ServerConfig {
            float tickInterval = 0.016667f;
        };

        [[nodiscard]] WorldError Init(Framework::Networking::NetworkPeer *networkPeer, ServerConfig cfg);

        void Shutdown() override;
        void Update() override;

        // Native entity lifecycle. CreateEntity constructs a registered entity type and starts
        // replicating it; the caller fills in its state and (for players) registers it as a viewer.
        Replication::NetworkEntity *CreateEntity(uint32_t typeId) const;
        void RemoveEntity(Replication::NetworkEntity *entity) const;

        void SetOwner(Replication::NetworkEntity *entity, uint64_t guid) const;
        uint64_t GetOwner(Replication::NetworkEntity *entity) const;

      private:
        ServerConfig _cfg {};
    };
} // namespace Framework::World
