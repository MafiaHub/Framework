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

namespace Framework::World {
    class ServerEngine final : public Engine {
      public:
        struct ServerConfig {
            float tickInterval = 0.016667f;
        };

        [[nodiscard]] WorldError Init(Framework::Networking::NetworkPeer *networkPeer, ServerConfig cfg);

        void Shutdown() override;
        void Update() override;

        // CreateEntity constructs a registered entity type and starts replicating it; the caller
        // fills in its state and (for players) registers it as a viewer on the ReplicationManager.
        Replication::NetworkEntity *CreateEntity(uint32_t typeId) const;
        void RemoveEntity(Replication::NetworkEntity *entity) const;

        void SetOwner(Replication::NetworkEntity *entity, uint64_t guid) const;
        uint64_t GetOwner(Replication::NetworkEntity *entity) const;
    };
} // namespace Framework::World
