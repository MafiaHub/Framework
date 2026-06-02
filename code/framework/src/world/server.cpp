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
        // Relevance, ownership and serialization are handled by ReplicaManager3 (GridSectorizer
        // interest + per-entity QuerySerialization).
        const WorldError err = Engine::Init(networkPeer);
        if (err != WorldError::WORLD_NONE) {
            return err;
        }
        // Replicate entity updates at the configured tick rate (tickInterval is in seconds).
        if (auto *replication = GetReplication()) {
            replication->SetAutoSerializeInterval(static_cast<MafiaNet::Time>(cfg.tickInterval * 1000.0f));
        }
        return WorldError::WORLD_NONE;
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
            // Routes through the entity so the new owner is notified directly (serialize to an owner
            // is withheld); see NetworkEntity::SetOwner.
            entity->SetOwner(guid);
        }
    }

    uint64_t ServerEngine::GetOwner(Replication::NetworkEntity *entity) const {
        return entity ? entity->ownerGUID : 0xFFFFFFFFFFFFFFFF;
    }
} // namespace Framework::World
