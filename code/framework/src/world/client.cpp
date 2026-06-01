/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "client.h"

namespace Framework::World {
    WorldError ClientEngine::Init() {
        return Engine::Init(nullptr); // peer assigned by OnConnect
    }

    void ClientEngine::Shutdown() {
        Engine::Shutdown();
    }

    void ClientEngine::Update() {
        Engine::Update();
    }

    void ClientEngine::OnConnect(Networking::NetworkPeer *peer, float tickInterval) {
        (void)tickInterval;
        _networkPeer = peer;
        // Nothing else to wire: ReplicaManager3 constructs/serializes/destroys entities natively.
        // Owned entities serialize upstream automatically via NetworkEntity::QuerySerialization.
    }

    void ClientEngine::OnDisconnect() {
        // Entity teardown is handled natively by ReplicaManager3 when the connection drops
        // (QueryActionOnPopConnection_Client deletes server-created replicas).
        _networkPeer = nullptr;
    }
} // namespace Framework::World
