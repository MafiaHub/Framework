/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "entities/entity.h"

#include <RakNetTypes.h>
#include <map>
#include <queue>
#include <vector>

namespace Framework::Networking {
    class EntityManager {
      public:
        EntityManager() = default;

        ~EntityManager();

        virtual void Update();

        virtual void AddEntity(Entities::Entity *);

        virtual Entities::Entity *CreateEntity(SLNet::RakNetGUID) = 0;

        virtual void DeleteEntity(uint64_t);

        bool HasEntity(uint64_t) const;

        Entities::Entity *GetEntity(uint64_t) const;

        size_t GetSize() const {
            return _entities.size();
        };

        std::map<uint64_t, Entities::Entity *> GetEntities() const {
            return _entities;
        }

        virtual uint64_t FindFreeId() {
            return _entityIndex++;
        };

        std::vector<Entities::Entity *> GetEntitiesVector() const;

        std::vector<Entities::Entity *> GetEntitiesOfType(Entities::Entity::EntityType kind) const;

      protected:
        void RemovePendingEntities();

        std::map<uint64_t, Entities::Entity *> _entities;

      private:
        uint64_t _entityIndex = 0;
        std::queue<std::pair<uint64_t, Entities::Entity *>> _pendingRemovals;
    };
} // namespace Framework::Networking
