/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "engine.h"

#include "modules/base.hpp"

namespace Framework::World {
    WorldError Engine::Init(Networking::NetworkPeer *networkPeer) {
        _networkPeer = networkPeer;
        _world       = std::make_unique<flecs::world>();

        // Register a base module
        _world->import <Modules::Base>();

        // Publish the peer as a singleton so systems and observers can read it
        // without capturing the Engine in their lambdas. Updated by clients
        // through SetNetworkPeer() when the connection (re)opens.
        _world->set<Modules::Base::NetworkPeerHandle>({networkPeer});

        // Both queries are iterated multiple times per tick, so we ask Flecs
        // to keep them fully cached. Without cached(), Flecs v4 queries are
        // uncached by default and rebuild their match set on every iteration.
        _allStreamableEntities   = _world->query_builder<Modules::Base::Transform, Modules::Base::Streamable>().cached().build();
        _findAllStreamerEntities = _world->query_builder<Modules::Base::Streamer>().cached().build();

        RegisterStreamerGuidCacheObservers();

        _initialized = true;
        return WorldError::WORLD_NONE;
    }

    void Engine::SetNetworkPeer(Networking::NetworkPeer *peer) {
        _networkPeer = peer;
        if (_world) {
            _world->set<Modules::Base::NetworkPeerHandle>({peer});
        }
    }

    void Engine::RegisterStreamerGuidCacheObservers() {
        // Maintain the guid -> entity map via observers instead of scanning all
        // Streamer entities on every lookup. OnSet fires both when the component
        // is added and whenever it is reassigned (including guid mutations made
        // through ensure/try_get_mut + modified<>()).
        _world->observer<Modules::Base::Streamer>()
            .event(flecs::OnSet)
            .each([this](flecs::entity e, Modules::Base::Streamer &s) {
                // Drop the stale forward entry if this entity's guid was
                // reassigned — OnSet only sees the new value.
                auto prev = _entityToGuid.find(e.id());
                if (prev != _entityToGuid.end() && prev->second != s.guid) {
                    auto fwd = _guidToEntity.find(prev->second);
                    if (fwd != _guidToEntity.end() && fwd->second == e.id()) {
                        _guidToEntity.erase(fwd);
                    }
                }
                _guidToEntity[s.guid]  = e.id();
                _entityToGuid[e.id()] = s.guid;
            });

        _world->observer<Modules::Base::Streamer>()
            .event(flecs::OnRemove)
            .each([this](flecs::entity e, Modules::Base::Streamer &s) {
                auto it = _guidToEntity.find(s.guid);
                if (it != _guidToEntity.end() && it->second == e.id()) {
                    _guidToEntity.erase(it);
                }
                _entityToGuid.erase(e.id());
            });
    }

    void Engine::Shutdown() {
        Lifecycle::Shutdown();
    }

    void Engine::Update() {
        _world->progress();
    }

    bool Engine::IsEntityOwner(flecs::entity e, uint64_t guid) {
        const auto es = e.try_get<Framework::World::Modules::Base::Streamable>();
        if (!es) {
            return false;
        }
        return (es->owner == guid);
    }

    void Engine::WakeEntity(flecs::entity e) {
        if (!e.has<Framework::World::Modules::Base::TickRateRegulator>()) {
            return;
        }
        const auto tr = e.try_get_mut<Framework::World::Modules::Base::TickRateRegulator>();
        tr->lastGenID--;
        const auto es      = e.try_get_mut<Framework::World::Modules::Base::Streamable>();
        es->updateInterval = es->defaultUpdateInterval;
    }

    flecs::entity Engine::GetEntityByGUID(uint64_t guid) const {
        const auto it = _guidToEntity.find(guid);
        if (it == _guidToEntity.end()) {
            return flecs::entity::null();
        }
        flecs::entity e(_world->get_world(), it->second);
        if (!e.is_alive()) {
            return flecs::entity::null();
        }
        return e;
    }

    flecs::entity Engine::WrapEntity(flecs::entity_t serverID) const {
        return flecs::entity(_world->get_world(), serverID);
    }

    void Engine::PurgeAllResourceEntities() const {
        _world->defer_begin();
        // RemovedOnResourceReload is a zero-size tag, so the each() callback
        // takes only the entity.
        _findAllResourceEntities.each([](flecs::entity e) {
            if (e.is_alive())
                e.add<Modules::Base::PendingRemoval>();
        });
        _world->defer_end();
    }
} // namespace Framework::World
