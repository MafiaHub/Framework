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
    EngineError Engine::Init(Networking::NetworkPeer *networkPeer) {
        CoreModules::SetWorldEngine(this);
        _networkPeer = networkPeer;
        _world       = std::make_unique<flecs::world>();

        // Register a base module
        _world->import <Modules::Base>();

        _allStreamableEntities   = _world->query_builder<Modules::Base::Transform, Modules::Base::Streamable>().build();
        _findAllStreamerEntities = _world->query_builder<Modules::Base::Streamer>().build();

        // Observer to maintain GUID cache
        _world->observer<Modules::Base::Streamer>("GUIDCacheUpdate")
            .event(flecs::OnSet)
            .each([this](flecs::entity e, Modules::Base::Streamer& s) {
                _guidCache[s.guid] = e;
            });

        _world->observer<Modules::Base::Streamer>("GUIDCacheRemove")
            .event(flecs::OnRemove)
            .each([this](flecs::entity e, Modules::Base::Streamer& s) {
                _guidCache.erase(s.guid);
            });

        return EngineError::ENGINE_NONE;
    }

    EngineError Engine::Shutdown() {
        CoreModules::SetWorldEngine(nullptr);
        return EngineError::ENGINE_NONE;
    }

    void Engine::Update() {
        _world->progress();
    }

    bool Engine::IsEntityOwner(flecs::entity e, uint64_t guid) {
        const auto es = e.get<Framework::World::Modules::Base::Streamable>();
        if (!es) {
            return false;
        }
        return (es->owner == guid);
    }

    void Engine::WakeEntity(flecs::entity e) {
        if (!e.get<Framework::World::Modules::Base::TickRateRegulator>()) {
            return;
        }
        const auto tr = e.get_mut<Framework::World::Modules::Base::TickRateRegulator>();
        tr->lastGenID--;
        const auto es      = e.get_mut<Framework::World::Modules::Base::Streamable>();
        es->updateInterval = es->defaultUpdateInterval;
    }

    flecs::entity Engine::GetEntityByGUID(uint64_t guid) const {
        auto it = _guidCache.find(guid);
        if (it != _guidCache.end() && it->second.is_alive()) {
            return it->second;
        }
        return flecs::entity::null();
    }

    flecs::entity Engine::WrapEntity(flecs::entity_t serverID) const {
        return flecs::entity(_world->get_world(), serverID);
    }

    void Engine::PurgeAllResourceEntities() const {
        _world->defer_begin();
        _findAllResourceEntities.run([this](flecs::iter& it) {
            while (it.next()) {
                for (auto i : it) {
                    auto e = it.entity(i);
                    if (e.is_alive())
                        e.add<Modules::Base::PendingRemoval>();
                }
            }
        });
        _world->defer_end();
    }
} // namespace Framework::World
