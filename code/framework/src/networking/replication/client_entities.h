/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "entity_registry.h"
#include "network_entity.h"
#include "replication_manager.h"

#include <function2/function2.hpp>

#include <type_traits>

namespace Framework::Networking::Replication {
    // Client: replicated entities by pointer. A client has one thread, so it reaches its entities
    // directly and may keep the pointers it is given until the server destroys them.
    //
    // Not for a server, where an entity is only safe behind a handle of the World: this class is
    // compiled into FrameworkClient alone, so a server target that uses it does not link, and it
    // finds nothing on a manager that runs as a server.
    class ClientEntities final {
      public:
        explicit ClientEntities(const ReplicationManager &manager): _manager(manager) {}

        NetworkEntity *GetEntityByNetworkID(MafiaNet::NetworkID networkId) const;
        void ForEachEntity(const fu2::function<void(NetworkEntity *) const> &fn) const;
        // Every player's avatar with the guid of the player it stands for: the entities the server
        // flagged as a connection's viewer. What proximity voice keys a speaker's position on.
        void ForEachAvatar(const fu2::function<void(MafiaNet::PeerGuid, NetworkEntity *) const> &fn) const;

        // Typed wrappers (an exact-type match, see CheckedEntityCast).
        template <typename T>
        T *GetEntity(MafiaNet::NetworkID networkId) const {
            return CheckedEntityCast<T>(GetEntityByNetworkID(networkId));
        }
        template <typename T>
        void ForEach(const fu2::function<void(T *) const> &fn) const {
            ForEachEntity([&fn](NetworkEntity *entity) {
                if (T *typed = CheckedEntityCast<T>(entity)) {
                    fn(typed);
                }
            });
        }

      private:
        const ReplicationManager &_manager;
    };
} // namespace Framework::Networking::Replication
