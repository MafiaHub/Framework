/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "engine.h"

// A client sends RPCs to the server with FW_BROADCAST_RPC (the client's only connection is the
// server). Entity identity, if any, travels as a NetworkID field inside the payload. To observe
// entity destruction, override Replication::NetworkEntity::DeallocReplica in your entity subclass —
// ReplicaManager3 calls it when the server removes the replica or the connection drops.

namespace Framework::World {
    class ClientEngine final : public Engine {
      public:
        [[nodiscard]] WorldError Init();

        void Shutdown() override;

        void OnConnect(Networking::NetworkPeer *peer, float tickInterval);
        void OnDisconnect();

        void Update() override;
    };
} // namespace Framework::World
