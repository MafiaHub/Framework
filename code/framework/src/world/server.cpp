/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "server.h"

namespace Framework::World {
    WorldError ServerEngine::Init(Framework::Networking::NetworkPeer *networkPeer, ServerConfig cfg) {
        _cfg = cfg;
        // Relevance, ownership and serialization are handled by ReplicaManager3 (GridSectorizer
        // interest + per-entity QuerySerialization).
        return Engine::Init(networkPeer);
    }

    void ServerEngine::Shutdown() {
        Engine::Shutdown();
    }

    void ServerEngine::Update() {
        Engine::Update();
    }

    Replication::NetworkEntity *ServerEngine::CreateEntity(uint32_t typeId) const {
        auto *replication = GetReplication();
        return replication ? replication->CreateEntity(typeId) : nullptr;
    }

    void ServerEngine::RemoveEntity(Replication::NetworkEntity *entity) const {
        if (auto *replication = GetReplication()) {
            replication->DestroyEntity(entity);
        }
    }

    void ServerEngine::SetOwner(Replication::NetworkEntity *entity, uint64_t guid) const {
        if (entity) {
            entity->ownerGUID = guid;
        }
    }

    uint64_t ServerEngine::GetOwner(Replication::NetworkEntity *entity) const {
        return entity ? entity->ownerGUID : 0xFFFFFFFFFFFFFFFF;
    }
} // namespace Framework::World
