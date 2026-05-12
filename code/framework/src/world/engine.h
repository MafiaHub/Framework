/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "modules/base.hpp"

#include <utils/lifecycle.h>

#include "networking/network_peer.h"

#include <flecs/distr/flecs.h>
#include <memory>
#include <unordered_map>

#include "core_modules.h"

#define FW_SEND_COMPONENT_RPC(rpc, ...)                                                                                                                                                                                                                                                \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        auto __net = Framework::CoreModules::GetNetworkPeer();                                                                                                                                                                                                                         \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendRPC<rpc>(s);                                                                                                                                                                                                                                                    \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

#define FW_SEND_COMPONENT_RPC_TO(rpc, guid, ...)                                                                                                                                                                                                                                       \
    do {                                                                                                                                                                                                                                                                               \
        auto s = rpc {};                                                                                                                                                                                                                                                               \
        s.FromParameters(__VA_ARGS__);                                                                                                                                                                                                                                                 \
        auto __net = Framework::CoreModules::GetNetworkPeer();                                                                                                                                                                                                                         \
        if (__net) {                                                                                                                                                                                                                                                                   \
            __net->SendRPC<rpc>(s, guid);                                                                                                                                                                                                                                              \
        }                                                                                                                                                                                                                                                                              \
    } while (0)

namespace Framework::Scripting {
    class ResourceManager;
}

namespace Framework::World {
    class Engine : public Lifecycle {
      private:
        friend class Framework::Scripting::ResourceManager;
        void PurgeAllResourceEntities() const;

      protected:
        // NOTE: Destruction order matters here.
        //   - Queries reference internal world state, so they must be destroyed
        //     *before* _world (queries are declared *after* _world below ->
        //     destroyed first in reverse declaration order).
        //   - The guid cache maps are touched by OnRemove observers that fire
        //     while _world is being destroyed, so they must outlive _world
        //     (declared *before* _world here -> destroyed last).
        // Cache of Streamer.guid -> entity id, maintained by observers on the Streamer component.
        std::unordered_map<uint64_t, flecs::entity_t> _guidToEntity;
        // Reverse map so we can drop the stale forward entry when an entity's
        // guid is reassigned (the OnSet observer only sees the new value).
        std::unordered_map<flecs::entity_t, uint64_t> _entityToGuid;
        std::unique_ptr<flecs::world> _world;
        flecs::query<Modules::Base::Streamer> _findAllStreamerEntities;
        flecs::query<Modules::Base::Transform, Modules::Base::Streamable> _allStreamableEntities;
        // Tag-only query, so the component list is empty and the entity is the
        // sole "field" produced by iteration.
        flecs::query<> _findAllResourceEntities;
        void RegisterStreamerGuidCacheObservers();
        // Publishes the active peer through the NetworkPeerHandle singleton.
        // The singleton is the only source of truth — there is no Engine-side
        // raw pointer to keep in sync.
        void SetNetworkPeer(Networking::NetworkPeer *peer);

      public:
        [[nodiscard]] WorldError Init(Networking::NetworkPeer *networkPeer);

        void Shutdown() override;

        void Update() override;

        flecs::entity GetEntityByGUID(uint64_t guid) const;
        flecs::entity WrapEntity(flecs::entity_t serverID) const;
        static bool IsEntityOwner(flecs::entity e, uint64_t guid);
        void WakeEntity(flecs::entity e);

        flecs::world *GetWorld() const {
            return _world.get();
        }

        // Convenience accessor for the NetworkPeerHandle singleton. Returns
        // nullptr when no world is initialized or the peer slot is empty —
        // the typical state during shutdown or before OnConnect on the client.
        Networking::NetworkPeer *GetNetworkPeer() const {
            if (!_world) return nullptr;
            const auto *h = _world->try_get<Modules::Base::NetworkPeerHandle>();
            return h ? h->peer : nullptr;
        }
    };
} // namespace Framework::World
