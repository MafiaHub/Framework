#include "entity_manager.h"

#include "logging/logger.h"

#include <optick.h>

namespace Framework::Networking {
    EntityManager::~EntityManager() {
        for (auto entity : _entities) { delete entity.second; }
    }

    void EntityManager::AddEntity(Entities::Entity *entity) {
        if (entity == nullptr) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->warn("[EntityManager] Added entity is nullptr");
            return;
        }

        const auto newId = FindFreeId();
        _entities[newId] = entity;
    }

    bool EntityManager::HasEntity(uint64_t id) const {
        return _entities.find(id) != _entities.end();
    }

    Entities::Entity *EntityManager::GetEntity(uint64_t id) const {
        auto it = _entities.find(id);
        if (it != _entities.end())
            return it->second;
        else
            return nullptr;
    }
    std::vector<Entities::Entity *> EntityManager::GetEntitiesVector() const {
        std::vector<Entities::Entity *> entities;

        for (auto ent : _entities) { entities.push_back(ent.second); }

        return entities;
    }
    std::vector<Entities::Entity *> EntityManager::GetEntitiesOfType(Entities::Entity::EntityType kind) const {
        std::vector<Entities::Entity *> entities;

        for (auto ent : _entities) {
            if (ent.second->GetType() == kind)
                entities.push_back(ent.second);
        }

        return entities;
    }
    void EntityManager::Update() {
        OPTICK_EVENT();

        RemovePendingEntities();

        for (auto &entity : _entities) { entity.second->Update(); }
    }
    void EntityManager::DeleteEntity(uint64_t id) {
        auto entity = GetEntity(id);
        if (entity == nullptr)
            return;

        entity->MarkForRemoval();
        _pendingRemovals.push(std::make_pair(id, entity));
    }
    void EntityManager::RemovePendingEntities() {
        while (!_pendingRemovals.empty()) {
            auto entity_pair = _pendingRemovals.front();
            _entities.erase(entity_pair.first);
            delete entity_pair.second;
            _pendingRemovals.pop();
        }
    }
} // namespace Framework::Networking
