/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

namespace Framework::World {
    WorldError Engine::Init(Networking::NetworkPeer *networkPeer) {
        _networkPeer = networkPeer;
        // flecs world backing the scripting resource tree.
        _world = std::make_unique<flecs::world>();

        _initialized = true;
        return WorldError::WORLD_NONE;
    }

    void Engine::Shutdown() {
        Lifecycle::Shutdown();
    }

    void Engine::Update() {
        // Advance the scripting resource tree; entity replication is driven by the network peer.
        if (_world) {
            _world->progress();
        }
    }

    Replication::NetworkEntity *Engine::GetEntityByNetworkID(MafiaNet::NetworkID networkId) const {
        auto *replication = GetReplication();
        return replication ? replication->GetEntityByNetworkID(networkId) : nullptr;
    }
} // namespace Framework::World
