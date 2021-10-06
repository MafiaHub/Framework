/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "entity_streamer.h"

#include "entities/entity.h"
#include "entity_manager.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <optick.h>

namespace Framework::Networking {
    void EntityStreamer::Update() {
        OPTICK_EVENT();

        auto players        = GetStreamerEntities();
        const auto entities = GetEntities();

        // Per each player (network client) we loop over all entities and compare the last snapshot with the current
        // stream state. We then send various events based on compared changes.
        for (auto player : players) {
            auto &snapshot = _streamedEntities[player->GetOwner().g];
            for (auto entity : entities) {
                if (player->GetNetworkID() == entity->GetNetworkID()) {
                    // Networked client can also receive updates, but in a different form.
                    EntitySelfUpdate(entity, player->GetOwner());
                    continue;
                }

                if (!entity->IsSpawned())
                    continue;

                // Determine if the entity is visible for our networked client and trigger respective event.
                bool canSend = CanSend(player, entity);
                if (std::find(snapshot.begin(), snapshot.end(), entity->GetNetworkID()) != snapshot.end()) {
                    if (!canSend) {
                        snapshot.erase(std::remove(snapshot.begin(), snapshot.end(), entity->GetNetworkID()));
                        EntityDespawn(entity, player->GetOwner());
                    } else if (player->GetOwner() != entity->GetOwner()) {
                        // Entity was already present in our last snapshot and is still visible, send update.
                        EntityUpdate(entity, player->GetOwner());
                    }
                } else if (canSend) {
                    snapshot.push_back(entity->GetNetworkID());
                    EntitySpawn(entity, player->GetOwner());
                }
            }
        }
    }

    bool EntityStreamer::CanSend(Entities::Entity *receiver, Entities::Entity *entity) {
        const float distanceBetweenEntities = glm::length(receiver->GetPosition() - entity->GetPosition());
        return entity->GetVirtualWorld() == receiver->GetVirtualWorld() && entity->IsSpawned() && !entity->IsPendingRemoval() && distanceBetweenEntities < GetStreamingRange();
    }

    void EntityStreamer::ClearEntity(Entities::Entity *entity) {
        const auto entityNetId = entity->GetNetworkID();
        auto players           = GetStreamerEntities();
        for (auto player : players) {
            if (player == entity)
                continue;

            auto &snapshot = _streamedEntities[player->GetOwner().g];
            if (std::find(snapshot.begin(), snapshot.end(), entityNetId) != snapshot.end()) {
                snapshot.erase(std::remove(snapshot.begin(), snapshot.end(), entityNetId));
                EntityDespawn(entity, player->GetOwner());
            }
        }
    }
    std::vector<Entities::Entity *> EntityStreamer::GetStreamerEntities() {
        std::vector<Entities::Entity *> entities;

        for (auto manager : _managers) {
            const auto man_entities = manager->GetEntitiesOfType(Entities::Entity::EntityType::PLAYER);
            entities.insert(entities.end(), man_entities.begin(), man_entities.end());
        }

        return entities;
    }
    std::vector<Entities::Entity *> EntityStreamer::GetEntities() {
        std::vector<Entities::Entity *> entities;

        for (auto manager : _managers) {
            const auto man_entities = manager->GetEntitiesVector();
            entities.insert(entities.end(), man_entities.begin(), man_entities.end());
        }

        return entities;
    }
} // namespace Framework::Networking
