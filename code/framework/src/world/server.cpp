/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "server.h"

#include "utils/time.h"

namespace Framework::World {
    WorldError ServerEngine::Init(Framework::Networking::NetworkPeer *networkPeer, ServerConfig cfg) {
        if (Engine::Init(networkPeer) != WorldError::WORLD_NONE) {
            return WorldError::WORLD_FLECS_INIT_FAILED;
        }

        _findAllResourceEntities = _world->query_builder().with<Modules::Base::RemovedOnResourceReload>().build();

        // Observer-driven removal: react the moment PendingRemoval is added to a
        // streamable entity, instead of polling on an interval. Flecs defers the
        // destruct call automatically so this is safe to invoke from within an
        // observer callback.
        _world->observer<Modules::Base::Streamable>("RemoveEntitiesOnPendingRemoval")
            .with<Modules::Base::PendingRemoval>()
            .event(flecs::OnAdd)
            .each([this](flecs::entity e, Modules::Base::Streamable &streamable) {
                _findAllStreamerEntities.each([this, &e, &streamable](flecs::entity rhsE, Modules::Base::Streamer &rhsS) {
                    if (rhsS.entities.contains(e)) {
                        rhsS.entities.erase(e);

                        if (streamable.GetBaseEvents().despawnProc)
                            streamable.GetBaseEvents().despawnProc(_networkPeer, rhsS.guid, e);
                    }
                });

                e.destruct();
            });

        // Set up a system to assign entity owners.
        _world->system<Modules::Base::Transform, Modules::Base::Streamable>("AssignEntityOwnership").kind(flecs::PostUpdate).interval(cfg.assignOwnershipTickInterval).each([this](flecs::entity e, Modules::Base::Transform &tr, Modules::Base::Streamable &streamable) {
            // Let user provide custom ownership assignment.
            if (streamable.assignOwnerManually || (streamable.assignOwnerProc && streamable.assignOwnerProc(e, streamable))) {
                /* no op */
            }
            else {
                // Assign the entity to the closest streamer.
                uint64_t closestOwnerGUID = SLNet::UNASSIGNED_RAKNET_GUID.g;
                float closestDist         = std::numeric_limits<float>::max();
                _findAllStreamerEntities.each([this, &e, &tr, &closestDist, &closestOwnerGUID, &streamable](flecs::entity rhsE, Modules::Base::Streamer &rhsS) {
                    const auto rhsTr      = rhsE.try_get<Modules::Base::Transform>();
                    const auto rhsRs      = rhsE.try_get<Modules::Base::Streamable>();
                    const auto canBeOwner = this->IsEntityVisibleToStreamer(rhsE, e, *rhsTr, rhsS, *rhsRs, tr, streamable);
                    if (canBeOwner) {
                        const auto dist = glm::distance(tr.pos, rhsTr->pos);
                        if (dist < closestDist) {
                            closestDist      = dist;
                            closestOwnerGUID = rhsS.guid;
                        }
                    }
                });

                streamable.owner = closestOwnerGUID;
            }
        });

        // Set up a system to collect stream range exempt entities.
        _world->system<Modules::Base::Streamer>("CollectRangeExemptEntities").kind(flecs::PostUpdate).interval(cfg.collectRangeExemptEntitiesTickInterval).each([this](flecs::entity e, Modules::Base::Streamer &streamer) {
            streamer.rangeExemptEntities.clear();
            if (streamer.collectRangeExemptEntitiesProc)
                streamer.collectRangeExemptEntitiesProc(e, streamer);
        });

        _world->system<Modules::Base::TickRateRegulator, Modules::Base::Transform, Modules::Base::Streamable>("TickRateRegulator").interval(cfg.tickRegulatorInterval).run([](flecs::iter &it) {
            while (it.next()) {
                const auto tr = it.field<Modules::Base::TickRateRegulator>(0);
                const auto t = it.field<Modules::Base::Transform>(1);
                const auto s = it.field<Modules::Base::Streamable>(2);

                for (auto i : it) {
                    bool decreaseRate       = true;
                    constexpr float EPSILON = 0.01f;
                    auto &snap              = tr[i].snapshot;

                    // Check if position has changed
                    if (glm::abs(t[i].pos.x - snap.pos.x) > EPSILON || glm::abs(t[i].pos.y - snap.pos.y) > EPSILON || glm::abs(t[i].pos.z - snap.pos.z) > EPSILON) {
                        decreaseRate = false;
                    }

                    // Check if rotation quaternion has changed
                    if (glm::abs(t[i].rot.x - snap.rot.x) > EPSILON || glm::abs(t[i].rot.y - snap.rot.y) > EPSILON || glm::abs(t[i].rot.z - snap.rot.z) > EPSILON || glm::abs(t[i].rot.w - snap.rot.w) > EPSILON) {
                        decreaseRate = false;
                    }

                    // Check if velocity has changed
                    if (glm::abs(t[i].vel.x - snap.vel.x) > EPSILON || glm::abs(t[i].vel.y - snap.vel.y) > EPSILON || glm::abs(t[i].vel.z - snap.vel.z) > EPSILON) {
                        decreaseRate = false;
                    }

                    // Check if generation ID has changed
                    if (t[i].GetGeneration() != tr[i].lastGenID) {
                        decreaseRate = true;
                    }

                    // Snapshot current transform for comparison on the next tick.
                    tr[i].lastGenID = t[i].GetGeneration();
                    snap.pos        = t[i].pos;
                    snap.rot        = t[i].rot;
                    snap.vel        = t[i].vel;

                    // Decrease tick rate if needed
                    if (decreaseRate) {
                        s[i].updateInterval += 5.0f;
                    }
                    else {
                        s[i].updateInterval = s[i].defaultUpdateInterval;
                    }
                }
            }
        });

        // Set up a system to stream entities to clients.
        _world->system<Modules::Base::Transform, Modules::Base::Streamer, Modules::Base::Streamable>("StreamEntities")
            .kind(flecs::PostUpdate)
            .interval(cfg.tickInterval)
            .run([this](flecs::iter &it) {
                while (it.next()) {
                    const auto tr = it.field<Modules::Base::Transform>(0);
                    const auto s = it.field<Modules::Base::Streamer>(1);
                    const auto rs = it.field<Modules::Base::Streamable>(2);

                    for (auto i : it) {
                        // Skip streamer entities we plan to remove.
                        if (it.entity(i).has<Modules::Base::PendingRemoval>())
                            continue;

                        // Grab all streamable entities.
                        _allStreamableEntities.each([&](flecs::entity e, Modules::Base::Transform &otherTr, Modules::Base::Streamable &otherS) {
                            // Skip dead entities.
                            if (!e.is_alive())
                                return;

                            // Let streamer send an update to self if an event is assigned.
                            if (e == it.entity(i) && rs[i].GetBaseEvents().selfUpdateProc && rs[i].performTickUpdates) {
                                rs[i].GetBaseEvents().selfUpdateProc(_networkPeer, s[i].guid, e);
                                return;
                            }

                            // Figure out entity visibility.
                            const auto id      = e.id();
                            const auto canSend = this->IsEntityVisibleToStreamer(it.entity(i), e, tr[i], s[i], rs[i], otherTr, otherS);
                            const auto map_it  = s[i].entities.find(id);

                            // Entity is already known to this streamer.
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
                                        if (otherS.GetBaseEvents().updateProc && rs[i].performTickUpdates)
                                            otherS.GetBaseEvents().updateProc(_networkPeer, s[i].guid, e);
                                        data.lastUpdate = static_cast<double>(Utils::Time::GetTime());
                                    }
                                }
                                else {
                                    auto &data = map_it->second;

                                    // If the entity is owned by this streamer, we send a full update.
                                    if (static_cast<double>(Utils::Time::GetTime()) - data.lastUpdate > otherS.updateInterval) {
                                        if (otherS.GetBaseEvents().ownerUpdateProc)
                                            otherS.GetBaseEvents().ownerUpdateProc(_networkPeer, s[i].guid, e);
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
                }
            });

        return WorldError::WORLD_NONE;
    }

    void ServerEngine::Shutdown() {
        Engine::Shutdown();
    }

    void ServerEngine::Update() {
        Engine::Update();
    }

    flecs::entity ServerEngine::CreateEntity(const std::string &name) const {
        if (name.empty()) {
            return _world->entity();
        }
        else {
            return _world->entity(name.c_str());
        }
    }

    void ServerEngine::SetOwner(flecs::entity e, uint64_t guid) {
        const auto es = e.try_get_mut<Framework::World::Modules::Base::Streamable>();
        if (!es) {
            return;
        }
        es->owner = guid;
    }

    flecs::entity ServerEngine::GetOwner(flecs::entity e) const {
        const auto es = e.try_get<Framework::World::Modules::Base::Streamable>();
        if (!es) {
            return flecs::entity::null();
        }
        return GetEntityByGUID(es->owner);
    }

    std::vector<flecs::entity> ServerEngine::FindVisibleStreamers(flecs::entity e) const {
        std::vector<flecs::entity> streamers;
        const auto es = e.try_get<Framework::World::Modules::Base::Streamable>();
        if (!es) {
            return {};
        }
        _findAllStreamerEntities.each([this, e, &streamers, es](flecs::entity rhsE, Modules::Base::Streamer &rhsS) {
            const auto rhsTr = rhsE.try_get<Modules::Base::Transform>();
            const auto rhsST = rhsE.try_get<Modules::Base::Streamable>();
            const auto lhsTr = e.try_get<Modules::Base::Transform>();
            if (!rhsTr || !rhsST || !lhsTr) {
                return;
            }

            if (this->IsEntityVisibleToStreamer(rhsE, e, *rhsTr, rhsS, *rhsST, *lhsTr, *es)) {
                streamers.push_back(rhsE);
            }
        });
        return streamers;
    }

    bool ServerEngine::RemoveEntity(flecs::entity e) {
        if (e.is_alive() && !e.has<Modules::Base::PendingRemoval>()) {
            e.add<Modules::Base::PendingRemoval>();
            return true;
        }
        return false;
    }

    bool ServerEngine::IsEntityVisibleToStreamer(const flecs::entity streamerEntity, const flecs::entity e, const Modules::Base::Transform &lhsTr, const Modules::Base::Streamer &streamer, const Modules::Base::Streamable &lhsS, const Modules::Base::Transform &rhsTr,
        const Modules::Base::Streamable& rhsS) const
    {
        std::unordered_set<flecs::entity_t> visited;
        return IsEntityVisibleToStreamerInternal(streamerEntity, e, lhsTr, streamer, lhsS, rhsTr, rhsS, visited);
    }

    bool ServerEngine::IsEntityVisibleToStreamerInternal(const flecs::entity streamerEntity, const flecs::entity e, const Modules::Base::Transform &lhsTr, const Modules::Base::Streamer &streamer, const Modules::Base::Streamable &lhsS, const Modules::Base::Transform &rhsTr,
        const Modules::Base::Streamable& rhsS, std::unordered_set<flecs::entity_t> &visited) const
    {
        if (!e.is_valid())
            return false;
        if (!e.is_alive())
            return false;

        // Discard entities that we plan to remove.
        if (e.has<Modules::Base::PendingRemoval>())
            return false;

        // Allow user to override visibility rules completely.
        if (rhsS.isVisibleProc && rhsS.isVisibleHeuristic == Modules::Base::Streamable::HeuristicMode::REPLACE) {
            return rhsS.isVisibleProc(streamerEntity, e);
        }

        // Mark this entity as visited to prevent infinite recursion in cyclic dependencies.
        if (!visited.insert(e.id()).second) {
            // Already visited - we're in a cycle, skip dependent check for this entity.
            // Continue with the remaining visibility checks.
        }
        else {
            // Check our dependents via the (DependsOn, *) relation. If any
            // target is visible, we are visible as well. Iterating the relation
            // pairs avoids maintaining a separate vector on the component and
            // keeps the dependency graph queryable via Flecs.
            bool dependentVisible = false;
            e.each<Modules::Base::DependsOn>([&](flecs::entity dependentEntity) {
                if (dependentVisible)
                    return;
                if (!dependentEntity.is_valid() || !dependentEntity.is_alive())
                    return;
                if (e == dependentEntity)
                    return;
                if (visited.contains(dependentEntity.id()))
                    return;
                const auto dependentS  = dependentEntity.try_get<Modules::Base::Streamable>();
                const auto dependentTr = dependentEntity.try_get<Modules::Base::Transform>();
                if (!dependentS || !dependentTr)
                    return;
                if (IsEntityVisibleToStreamerInternal(streamerEntity, dependentEntity, lhsTr, streamer, lhsS, *dependentTr, *dependentS, visited)) {
                    dependentVisible = true;
                }
            });
            if (dependentVisible)
                return true;
        }

        // Entity is always visible to clients.
        if (rhsS.alwaysVisible)
            return true;

        // Entity can be hidden from clients.
        if (!rhsS.isVisible)
            return false;

        // Validate if the entity resides in the same virtual world client does.
        if (lhsS.virtualWorld != rhsS.virtualWorld)
            return false;

        // Let user replace the distance check.
        if (rhsS.isVisibleProc && rhsS.isVisibleHeuristic == Modules::Base::Streamable::HeuristicMode::REPLACE_POSITION) {
            return rhsS.isVisibleProc(streamerEntity, e);
        }

        // Perform distance check.
        const auto dist = glm::distance(lhsTr.pos, rhsTr.pos);
        auto isVisible  = dist < streamer.range;

        // If we made it this far and the entity is streaming range check exempt
        // we override isVisible state to True.
        if (streamer.rangeExemptEntities.contains(e.id())) {
            isVisible = true;
        }

        // Allow user to provide additional rules for visibility.
        if (rhsS.isVisibleProc && rhsS.isVisibleHeuristic == Modules::Base::Streamable::HeuristicMode::ADD) {
            isVisible = isVisible && rhsS.isVisibleProc(streamerEntity, e);
        }

        return isVisible;
    }
} // namespace Framework::World
