#pragma once

#include <cstdint>
#include <slikenet/types.h>
#include <unordered_map>
#include <vector>

namespace Framework::Networking {
    namespace Entities {
        class Entity;
    }
    class EntityManager;

    class EntityStreamer {
      public:
        EntityStreamer() = default;

        std::vector<EntityManager *> &GetEntityManagers() {
            return _managers;
        }

        void Update();

        bool CanSend(Entities::Entity *receiver, Entities::Entity *entity);

        void ClearEntity(Entities::Entity *entity);

      protected:
        virtual void EntitySpawn(Entities::Entity *entity, SLNet::RakNetGUID ownerGUID) = 0;

        virtual void EntityDespawn(Entities::Entity *entity, SLNet::RakNetGUID ownerGUID) = 0;

        virtual void EntityUpdate(Entities::Entity *entity, SLNet::RakNetGUID ownerGUID) = 0;

        virtual void EntitySelfUpdate(Entities::Entity *entity, SLNet::RakNetGUID ownerGUID) = 0;

        virtual float GetStreamingRange() const {
            return 350.0f;
        }

        std::vector<Entities::Entity *> GetStreamerEntities();

        std::vector<Entities::Entity *> GetEntities();

      private:
        // uin64_t -> RakNet::GUID
        std::unordered_map<uint64_t, std::vector<SLNet::NetworkID>> _streamedEntities;
        std::vector<EntityManager *> _managers;
    };
} // namespace Framework::Networking
