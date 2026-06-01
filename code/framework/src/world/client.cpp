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
        _networkPeer = peer;
        // Serialize owned entities upstream at the server's tick rate (tickInterval is in seconds).
        if (auto *replication = GetReplication()) {
            replication->SetAutoSerializeInterval(static_cast<MafiaNet::Time>(tickInterval * 1000.0f));
        }
    }

    void ClientEngine::OnDisconnect() {
        // Entity teardown is handled natively by ReplicaManager3 when the connection drops
        // (QueryActionOnPopConnection_Client deletes server-created replicas).
        _networkPeer = nullptr;
    }
} // namespace Framework::World
