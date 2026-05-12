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

        _findAllResourceEntities = _world->query_builder().with<Modules::Base::RemovedOnResourceReload>().cached().build();

        // Declare custom phases so server systems express their order via
        // DependsOn relations instead of all crowding into PostUpdate. Each
        // phase runs after the previous one within a single world.progress().
        const auto ownershipPhase = _world->entity("OwnershipPhase").add(flecs::Phase).depends_on(flecs::PostUpdate);
        const auto streamingPhase = _world->entity("StreamingPhase").add(flecs::Phase).depends_on(ownershipPhase);

        // Translate StreamSpawnEvent/etc. into network messages. Mods can
        // subscribe to the same events for additional behavior; the per-entity
        // modEvents.* callbacks are invoked by these observers as well.
        Modules::Base::RegisterServerStreamObservers(*_world);

        // Observer-driven removal: react the moment PendingRemoval is added,
        // instead of polling on an interval. Anchoring on PendingRemoval (not
        // Streamable) ensures the observer only fires for transitions of the
        // tag itself — adding Streamable to an entity that already carries
        // PendingRemoval would otherwise re-trigger the destruct path. Flecs
        // defers the destruct call automatically so it is safe to invoke from
        // within an observer callback.
        _world->observer<>("RemoveEntitiesOnPendingRemoval")
            .with<Modules::Base::PendingRemoval>()
            .event(flecs::OnAdd)
            .each([this](flecs::entity e) {
                if (!e.has<Modules::Base::Streamable>()) {
                    // Non-streamable entity: nothing to despawn from streamers,
                    // just destroy it.
                    e.destruct();
                    return;
                }
                const auto peer = GetNetworkPeer();

                _findAllStreamerEntities.each([this, &e, peer](flecs::entity rhsE, Modules::Base::Streamer &rhsS) {
                    if (rhsS.entities.contains(e)) {
                        rhsS.entities.erase(e);
                        if (!peer) return;
                        _world->event<Modules::Base::StreamDespawnEvent>()
                            .id<Modules::Base::Streamable>()
                            .entity(e)
                            .ctx(Modules::Base::StreamDespawnEvent{{peer, rhsS.guid}})
                            .emit();
                    }
                });

                e.destruct();
            });

        // Set up a system to assign entity owners.
        _world->system<Modules::Base::Transform, Modules::Base::Streamable>("AssignEntityOwnership").kind(ownershipPhase).interval(cfg.assignOwnershipTickInterval).each([this](flecs::entity e, Modules::Base::Transform &tr, Modules::Base::Streamable &streamable) {
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
        _world->system<Modules::Base::Streamer>("CollectRangeExemptEntities").kind(streamingPhase).interval(cfg.collectRangeExemptEntitiesTickInterval).each([this](flecs::entity e, Modules::Base::Streamer &streamer) {
            streamer.rangeExemptEntities.clear();
            if (streamer.collectRangeExemptEntitiesProc)
                streamer.collectRangeExemptEntitiesProc(e, streamer);
        });

        // Multi-threaded: this system only mutates its own entities' regulator
        // snapshot and updateInterval. No event emission, no structural ops,
        // safe to split across worker threads when the world is configured
        // with set_threads(N). Single-threaded for N=1.
        _world->system<Modules::Base::TickRateRegulator, Modules::Base::Transform, Modules::Base::Streamable>("TickRateRegulator")
            .multi_threaded()
            .interval(cfg.tickRegulatorInterval)
            .each([](Modules::Base::TickRateRegulator &reg, Modules::Base::Transform &t, Modules::Base::Streamable &s) {
                constexpr float EPSILON = 0.01f;
                auto &snap              = reg.snapshot;

                bool decreaseRate = true;
                if (glm::abs(t.pos.x - snap.pos.x) > EPSILON || glm::abs(t.pos.y - snap.pos.y) > EPSILON || glm::abs(t.pos.z - snap.pos.z) > EPSILON) {
                    decreaseRate = false;
                }
                if (glm::abs(t.rot.x - snap.rot.x) > EPSILON || glm::abs(t.rot.y - snap.rot.y) > EPSILON || glm::abs(t.rot.z - snap.rot.z) > EPSILON || glm::abs(t.rot.w - snap.rot.w) > EPSILON) {
                    decreaseRate = false;
                }
                if (glm::abs(t.vel.x - snap.vel.x) > EPSILON || glm::abs(t.vel.y - snap.vel.y) > EPSILON || glm::abs(t.vel.z - snap.vel.z) > EPSILON) {
                    decreaseRate = false;
                }
                // A generation mismatch is the deliberate wake signal raised
                // by Engine::WakeEntity() (which also resets updateInterval to
                // default). Treat it as "stay at the high rate", not as a
                // slowdown — the previous code did the opposite and silently
                // undid the wake on the very next tick.
                if (t.GetGeneration() != reg.lastGenID) {
                    decreaseRate = false;
                }

                reg.lastGenID = t.GetGeneration();
                snap.pos      = t.pos;
                snap.rot      = t.rot;
                snap.vel      = t.vel;

                if (decreaseRate) {
                    s.updateInterval += 5.0f;
                } else {
                    s.updateInterval = s.defaultUpdateInterval;
                }
            });

        // Set up a system to stream entities to clients. Visibility decisions
        // here translate into custom-event emissions on the streamable entity;
        // the framework's network observers (RegisterServerStreamObservers)
        // and any mod-registered observers convert those events into messages.
        _world->system<Modules::Base::Transform, Modules::Base::Streamer, Modules::Base::Streamable>("StreamEntities")
            .kind(streamingPhase)
            .interval(cfg.tickInterval)
            .run([this](flecs::iter &it) {
                const auto peer = GetNetworkPeer();
                if (!peer) return; // no peer, nothing to send

                while (it.next()) {
                    const auto tr = it.field<Modules::Base::Transform>(0);
                    const auto s = it.field<Modules::Base::Streamer>(1);
                    const auto rs = it.field<Modules::Base::Streamable>(2);

                    for (auto i : it) {
                        // Skip streamer entities we plan to remove.
                        if (it.entity(i).has<Modules::Base::PendingRemoval>())
                            continue;

                        _allStreamableEntities.each([&](flecs::entity e, Modules::Base::Transform &otherTr, Modules::Base::Streamable &otherS) {
                            if (!e.is_alive())
                                return;

                            const auto guid = s[i].guid;

                            // Let streamer send an update to self.
                            if (e == it.entity(i) && rs[i].performTickUpdates) {
                                _world->event<Modules::Base::StreamSelfUpdateEvent>()
                                    .id<Modules::Base::Streamable>()
                                    .entity(e)
                                    .ctx(Modules::Base::StreamSelfUpdateEvent{{peer, guid}})
                                    .emit();
                                return;
                            }

                            const auto id      = e.id();
                            const auto canSend = this->IsEntityVisibleToStreamer(it.entity(i), e, tr[i], s[i], rs[i], otherTr, otherS);
                            const auto map_it  = s[i].entities.find(id);

                            if (map_it != s[i].entities.end()) {
                                if (!canSend) {
                                    s[i].entities.erase(map_it);
                                    _world->event<Modules::Base::StreamDespawnEvent>()
                                        .id<Modules::Base::Streamable>()
                                        .entity(e)
                                        .ctx(Modules::Base::StreamDespawnEvent{{peer, guid}})
                                        .emit();
                                }
                                else if (rs[i].owner != otherS.owner) {
                                    auto &data = map_it->second;
                                    if (static_cast<double>(Utils::Time::GetTime()) - data.lastUpdate > otherS.updateInterval) {
                                        if (rs[i].performTickUpdates) {
                                            _world->event<Modules::Base::StreamUpdateEvent>()
                                                .id<Modules::Base::Streamable>()
                                                .entity(e)
                                                .ctx(Modules::Base::StreamUpdateEvent{{peer, guid}})
                                                .emit();
                                        }
                                        data.lastUpdate = static_cast<double>(Utils::Time::GetTime());
                                    }
                                }
                                else {
                                    auto &data = map_it->second;
                                    if (static_cast<double>(Utils::Time::GetTime()) - data.lastUpdate > otherS.updateInterval) {
                                        _world->event<Modules::Base::StreamOwnerUpdateEvent>()
                                            .id<Modules::Base::Streamable>()
                                            .entity(e)
                                            .ctx(Modules::Base::StreamOwnerUpdateEvent{{peer, guid}})
                                            .emit();
                                        data.lastUpdate = static_cast<double>(Utils::Time::GetTime());
                                    }
                                }
                            }
                            else if (canSend) {
                                // Contract change vs. the legacy spawnProc API:
                                // spawn observers cannot reject a spawn — the
                                // entity is recorded in the streamer's set
                                // unconditionally. The previous bool-returning
                                // spawnProc was always implemented to return
                                // true, so this matches existing behavior; if
                                // a future mod needs veto semantics, extend
                                // StreamSpawnEvent with an `accepted` flag the
                                // emitter can read back after emit().
                                _world->event<Modules::Base::StreamSpawnEvent>()
                                    .id<Modules::Base::Streamable>()
                                    .entity(e)
                                    .ctx(Modules::Base::StreamSpawnEvent{{peer, guid}})
                                    .emit();
                                Modules::Base::Streamer::StreamData data;
                                data.lastUpdate   = static_cast<double>(Utils::Time::GetTime());
                                s[i].entities[id] = data;
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
