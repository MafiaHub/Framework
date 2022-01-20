/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "server.h"

#include "utils/time.h"

#include <optick.h>

namespace Framework::World {
    EngineError ServerEngine::Init(Framework::Networking::NetworkPeer *networkPeer, float tickInterval) {
        const auto status = Engine::Init(networkPeer);

        if (status != EngineError::ENGINE_NONE) {
            return status;
        }

        _isEntityVisible = [](const flecs::entity streamerEntity, const flecs::entity e, const Modules::Base::Transform &lhsTr, const Modules::Base::Streamer &streamer, const Modules::Base::Streamable &lhsS,
                             const Modules::Base::Transform &rhsTr, const Modules::Base::Streamable rhsS) -> bool {
            if (!e.is_valid())
                return false;
            if (!e.is_alive())
                return false;
            if (e.get<Modules::Base::PendingRemoval>() != nullptr)
                return false;

            if (rhsS.isVisibleProc && rhsS.isVisibleHeuristic == Modules::Base::Streamable::HeuristicMode::REPLACE) {
                return rhsS.isVisibleProc(streamerEntity, e);
            }

            if (rhsS.alwaysVisible)
                return true;
            if (!rhsS.isVisible)
                return false;
            if (lhsS.virtualWorld != rhsS.virtualWorld)
                return false;

            if (rhsS.isVisibleProc && rhsS.isVisibleHeuristic == Modules::Base::Streamable::HeuristicMode::REPLACE_POSITION) {
                return rhsS.isVisibleProc(streamerEntity, e);
            }

            const auto dist = glm::distance(lhsTr.pos, rhsTr.pos);
            auto isVisible  = dist < streamer.range;

            if (rhsS.isVisibleProc && rhsS.isVisibleHeuristic == Modules::Base::Streamable::HeuristicMode::ADD) {
                isVisible = isVisible && rhsS.isVisibleProc(streamerEntity, e);
            }

            return isVisible;
        };

        _world->system<Modules::Base::PendingRemoval, Modules::Base::Streamable>("RemoveEntities")
            .kind(flecs::PostUpdate)
            .interval(tickInterval * 4.0f)
            .each([this](flecs::entity e, Modules::Base::PendingRemoval &pd, Modules::Base::Streamable &streamable) {
                _findAllStreamerEntities.each([this, &e, &streamable](flecs::entity rhsE, Modules::Base::Streamer &rhsS) {
                    if (rhsS.entities.find(e) != rhsS.entities.end()) {
                        rhsS.entities.erase(e);
                        if (streamable.GetBaseEvents().despawnProc)
                            streamable.GetBaseEvents().despawnProc(_networkPeer, rhsS.guid, e);
                    }
                });

                e.destruct();
            });

        _world->system<Modules::Base::Transform, Modules::Base::Streamer, Modules::Base::Streamable>("StreamEntities")
            .kind(flecs::PostUpdate)
            .interval(tickInterval)
            .iter([this](flecs::iter it, Modules::Base::Transform *tr, Modules::Base::Streamer *s, Modules::Base::Streamable *rs) {
                for (size_t i = 0; i < it.count(); i++) {
                    OPTICK_EVENT();
                    if (it.entity(i).get<Modules::Base::PendingRemoval>() != nullptr)
                        continue;
                    _allStreamableEntities.each([&](flecs::entity e, Modules::Base::Transform &otherTr, Modules::Base::Streamable &otherS) {
                        if (!e.is_alive())
                            return;
                        if (e == it.entity(i) && rs[i].GetBaseEvents().selfUpdateProc) {
                            rs[i].GetBaseEvents().selfUpdateProc(_networkPeer, s[i].guid, e);
                            return;
                        }
                        const auto id      = e.id();
                        const auto canSend = _isEntityVisible(it.entity(i), e, tr[i], s[i], rs[i], otherTr, otherS);
                        const auto map_it  = s[i].entities.find(id);
                        if (map_it != s[i].entities.end()) {
                            // If we can't stream an entity anymore, despawn it
                            if (!canSend) {
                                s[i].entities.erase(map_it);
                                if (otherS.GetBaseEvents().despawnProc)
                                    otherS.GetBaseEvents().despawnProc(_networkPeer, s[i].guid, e);
                            }

                            // otherwise we do regular updates
                            else if (rs[i].owner != otherS.owner) {
                                auto &data = map_it->second;
                                if (static_cast<double>(Utils::Time::GetTime()) - data.lastUpdate > otherS.updateInterval) {
                                    if (otherS.GetBaseEvents().updateProc)
                                        otherS.GetBaseEvents().updateProc(_networkPeer, s[i].guid, e);
                                    data.lastUpdate = static_cast<double>(Utils::Time::GetTime());
                                }
                            }
                        }

                        // this is a new entity, spawn it unless user says otherwise
                        else if (canSend && otherS.GetBaseEvents().spawnProc) {
                            if (otherS.GetBaseEvents().spawnProc(_networkPeer, s[i].guid, e)) {
                                Modules::Base::Streamer::StreamData data;
                                data.lastUpdate   = static_cast<double>(Utils::Time::GetTime());
                                s[i].entities[id] = data;
                            }
                        }
                    });
                }
            });

        return EngineError::ENGINE_NONE;
    }

    EngineError ServerEngine::Shutdown() {
        return Engine::Shutdown();
    }

    void ServerEngine::Update() {
        OPTICK_EVENT();
        Engine::Update();
    }

    flecs::entity ServerEngine::CreateEntity(std::string name) {
        if (name.empty()) {
            return _world->entity();
        }
        else {
            return _world->entity(name.c_str());
        }
    }

    bool ServerEngine::IsEntityOwner(flecs::entity e, uint64_t guid) const {
        const auto es = e.get<Framework::World::Modules::Base::Streamable>();
        if (!es) {
            return false;
        }
        return (es->owner == guid);
    }

    void ServerEngine::SetOwner(flecs::entity e, uint64_t guid) {
        auto es = e.get_mut<Framework::World::Modules::Base::Streamable>();
        if (!es) {
            return;
        }
        es->owner = guid;
    }

    flecs::entity ServerEngine::GetOwner(flecs::entity e) const {
        const auto es = e.get<Framework::World::Modules::Base::Streamable>();
        if (!es) {
            return flecs::entity::null();
        }
        return GetEntityByGUID(es->owner);
    }

    std::vector<flecs::entity> ServerEngine::FindVisibleStreamers(flecs::entity e) const {
        std::vector<flecs::entity> streamers;
        const auto es = e.get<Framework::World::Modules::Base::Streamable>();
        if (!es) {
            return {};
        }
        _findAllStreamerEntities.each([this, e, &streamers, es](flecs::entity rhsE, Modules::Base::Streamer &rhsS) {
            const auto rhsTr = rhsE.get<Modules::Base::Transform>();
            const auto rhsST = rhsE.get<Modules::Base::Streamable>();
            const auto lhsTr = e.get<Modules::Base::Transform>();

            if (_isEntityVisible(rhsE, e, *rhsTr, rhsS, *rhsST, *lhsTr, *es)) {
                streamers.push_back(rhsE);
            }
        });
        return streamers;
    }


    void ServerEngine::RemoveEntity(flecs::entity e) {
        if (e.is_alive()) {
            e.add<Modules::Base::PendingRemoval>();
        }
    }
} // namespace Framework::World
