/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "networking/replication/entity_registry.h"
#include "networking/replication/network_entity.h"
#include "networking/replication/replication_manager.h"
#include "networking/replication/world.h"

#include <cstdint>

// The manager as the ReplicaManager3 it is, which is private to it: tests push connections and
// reference replicas by hand, with no network behind them.
struct Framework::Networking::Replication::TestAccess {
    static MafiaNet::ReplicaManager3 *Base(ReplicationManager *manager) {
        return manager;
    }
    static MafiaNet::PluginReceiveResult OnReceive(ReplicationManager *manager, MafiaNet::Packet *packet) {
        return manager->OnReceive(packet);
    }
    static void OnClosedConnection(ReplicationManager *manager, const MafiaNet::SystemAddress &address, MafiaNet::RakNetGUID guid, MafiaNet::PI2_LostConnectionReason reason) {
        manager->OnClosedConnection(address, guid, reason);
    }
};

// Server entities for tests, made and reached through the world as a game's are. A test drives the
// manager step by step on one thread and looks at an entity between two steps, so it keeps the
// pointer a handle gave it once the handle is gone; nothing but a test may.
namespace TestWorld {
    using Framework::Networking::Replication::NetworkEntity;
    using Framework::Networking::Replication::ReplicationManager;

    // The type id of a plain NetworkEntity, for tests that need nothing more.
    inline uint32_t PlainType() {
        static const uint32_t type = Framework::Networking::Replication::EntityRegistry::Get().Register<NetworkEntity>("Test::World::Plain");
        return type;
    }

    inline MafiaNet::ReplicaManager3 *Rm3(ReplicationManager *manager) {
        return Framework::Networking::Replication::TestAccess::Base(manager);
    }

    inline MafiaNet::PluginReceiveResult OnReceive(ReplicationManager *manager, MafiaNet::Packet *packet) {
        return Framework::Networking::Replication::TestAccess::OnReceive(manager, packet);
    }

    inline void OnClosedConnection(ReplicationManager *manager, const MafiaNet::SystemAddress &address, MafiaNet::RakNetGUID guid, MafiaNet::PI2_LostConnectionReason reason) {
        Framework::Networking::Replication::TestAccess::OnClosedConnection(manager, address, guid, reason);
    }

    inline NetworkEntity *Make(ReplicationManager *manager, uint32_t typeId) {
        const auto created = manager->GetWorld().Create(typeId);
        return created ? &*created : nullptr;
    }

    template <typename T>
    T *Make(ReplicationManager *manager) {
        const auto created = manager->GetWorld().template Create<T>();
        return created ? &*created : nullptr;
    }

    inline NetworkEntity *Find(ReplicationManager *manager, uint64_t id) {
        const auto found = manager->GetWorld().Write<NetworkEntity>(id);
        return found ? &*found : nullptr;
    }

    template <typename T>
    T *Find(ReplicationManager *manager, uint64_t id) {
        const auto found = manager->GetWorld().template Write<T>(id);
        return found ? &*found : nullptr;
    }

    inline void Destroy(ReplicationManager *manager, NetworkEntity *entity) {
        manager->GetWorld().Destroy(entity->GetNetworkID());
    }

    // An entity that lives as long as this object, as one on the stack did.
    class Owned final {
      public:
        explicit Owned(ReplicationManager *manager, uint32_t typeId = PlainType()): _manager(manager), _entity(Make(manager, typeId)), id(_entity->GetNetworkID()) {}
        ~Owned() {
            _manager->GetWorld().Destroy(id);
        }
        Owned(const Owned &)            = delete;
        Owned &operator=(const Owned &) = delete;

        NetworkEntity *operator->() const {
            return _entity;
        }
        NetworkEntity &operator*() const {
            return *_entity;
        }
        NetworkEntity *get() const {
            return _entity;
        }

      private:
        ReplicationManager *_manager;
        NetworkEntity *_entity;

      public:
        const uint64_t id;
    };
} // namespace TestWorld
