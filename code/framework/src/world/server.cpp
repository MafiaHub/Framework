/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "server.h"

#include "networking/messages/game_sync/entity_messages.h"
#include "utils/time.h"

namespace Framework::World {
    EngineError ServerEngine::Init(Framework::Networking::NetworkPeer *networkPeer, ServerConfig cfg) {
        const auto status = Engine::Init(networkPeer);

        if (status != EngineError::ENGINE_NONE) {
            return status;
        }

        _findAllResourceEntities = _world->query_builder<Modules::Base::RemovedOnResourceReload>().build();

        // Specialized queries for decomposed visibility system (MP-7)
        // AlwaysVisible entities skip distance checks entirely
        _alwaysVisibleEntities = _world->query_builder<Modules::Base::Transform, Modules::Base::Streamable>()
            .with<Modules::Base::AlwaysVisible>()
            .without<Modules::Base::PendingRemoval>()
            .without<Modules::Base::Hidden>()
            .build();

        // Normal streamable entities (excludes AlwaysVisible and Hidden)
        _normalStreamableEntities = _world->query_builder<Modules::Base::Transform, Modules::Base::Streamable>()
            .without<Modules::Base::PendingRemoval>()
            .without<Modules::Base::Hidden>()
            .without<Modules::Base::AlwaysVisible>()
            .build();

        // Observer to sync OwnedBy relation to owner GUID for network messages
        _world->observer()
            .with<Modules::Base::OwnedBy>(flecs::Wildcard)
            .event(flecs::OnAdd)
            .each([](flecs::iter& it, size_t row) {
                auto e = it.entity(row);
                auto owner = it.pair(0).second();
                auto streamable = e.get_mut<Modules::Base::Streamable>();
                if (streamable && owner.is_valid()) {
                    auto streamer = owner.get<Modules::Base::Streamer>();
                    if (streamer) {
                        streamable->owner = streamer->guid;
                    }
                }
            });

        // Framework-level observer: Send spawn message when StreamedTo relation is added
        _world->observer("SpawnObserver")
            .with<Modules::Base::StreamedTo>(flecs::Wildcard)
            .event(flecs::OnSet)
            .each([this](flecs::iter& it, size_t row) {
                auto e = it.entity(row);
                auto streamerEntity = it.pair(0).second();

                if (!streamerEntity.is_valid() || !streamerEntity.is_alive())
                    return;

                auto streamer = streamerEntity.get<Modules::Base::Streamer>();
                if (!streamer)
                    return;

                auto tr = e.get<Modules::Base::Transform>();
                Networking::Messages::GameSyncEntitySpawn spawnMsg;
                if (tr)
                    spawnMsg.FromParameters(*tr);
                spawnMsg.SetServerID(e.id());
                _networkPeer->Send(spawnMsg, streamer->guid);
            });

        // Framework-level observer: Send despawn message when StreamedTo relation is removed
        _world->observer("DespawnObserver")
            .with<Modules::Base::StreamedTo>(flecs::Wildcard)
            .event(flecs::OnRemove)
            .each([this](flecs::iter& it, size_t row) {
                auto e = it.entity(row);
                auto streamerEntity = it.pair(0).second();

                if (!streamerEntity.is_valid() || !streamerEntity.is_alive())
                    return;

                auto streamer = streamerEntity.get<Modules::Base::Streamer>();
                if (!streamer)
                    return;

                Networking::Messages::GameSyncEntityDespawn despawnMsg;
                despawnMsg.SetServerID(e.id());
                _networkPeer->Send(despawnMsg, streamer->guid);
            });

        // Set up a system to remove entities we no longer need.
        _world->system<Modules::Base::PendingRemoval, Modules::Base::Streamable>("RemoveEntities").kind(flecs::PostUpdate).interval(cfg.removeEntitiesTickInterval).run([this](flecs::iter &it) {
            while (it.next()) {
                for (auto i : it) {
                    auto e = it.entity(i);

                    // Remove all StreamedTo relations (triggers despawn observers)
                    e.remove<Modules::Base::StreamedTo>(flecs::Wildcard);

                    e.destruct();
                }
            }
        });

        // Set up a system to assign entity owners.
        _world->system<Modules::Base::Transform, Modules::Base::Streamable>("AssignEntityOwnership").kind(flecs::PostUpdate).interval(cfg.assignOwnershipTickInterval).each([this](flecs::entity e, Modules::Base::Transform &tr, Modules::Base::Streamable &streamable) {
            // Let user provide custom ownership assignment.
            if (e.has<Modules::Base::ManualOwnership>() || (streamable.assignOwnerProc && streamable.assignOwnerProc(e, streamable))) {
                /* no op */
            }
            else {
                // Assign the entity to the closest streamer.
                uint64_t closestOwnerGUID = SLNet::UNASSIGNED_RAKNET_GUID.g;
                float closestDist         = std::numeric_limits<float>::max();
                _findAllStreamerEntities.each([this, &e, &tr, &closestDist, &closestOwnerGUID, &streamable](flecs::entity rhsE, Modules::Base::Streamer &rhsS) {
                    const auto rhsTr      = rhsE.get<Modules::Base::Transform>();
                    const auto rhsRs      = rhsE.get<Modules::Base::Streamable>();
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

                    // Check if position has changed
                    if (glm::abs(t[i].pos.x - tr[i].pos.x) > EPSILON || glm::abs(t[i].pos.y - tr[i].pos.y) > EPSILON || glm::abs(t[i].pos.z - tr[i].pos.z) > EPSILON) {
                        decreaseRate = false;
                    }

                    // Check if rotation quaternion has changed
                    if (glm::abs(t[i].rot.x - tr[i].rot.x) > EPSILON || glm::abs(t[i].rot.y - tr[i].rot.y) > EPSILON || glm::abs(t[i].rot.z - tr[i].rot.z) > EPSILON || glm::abs(t[i].rot.w - tr[i].rot.w) > EPSILON) {
                        decreaseRate = false;
                    }

                    // Check if velocity has changed
                    if (glm::abs(t[i].vel.x - tr[i].vel.x) > EPSILON || glm::abs(t[i].vel.y - tr[i].vel.y) > EPSILON || glm::abs(t[i].vel.z - tr[i].vel.z) > EPSILON) {
                        decreaseRate = false;
                    }

                    // Check if generation ID has changed
                    if (t[i].GetGeneration() != tr[i].lastGenID) {
                        decreaseRate = true;
                    }

                    // Update all values
                    tr[i].lastGenID = t[i].GetGeneration();
                    tr[i].pos       = t[i].pos;
                    tr[i].rot       = t[i].rot;
                    tr[i].vel       = t[i].vel;

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
        // This system manages StreamedTo relations based on visibility.
        // Decomposed into phases for efficiency (MP-7):
        // Phase 1: AlwaysVisible entities - always stream, skip distance check
        // Phase 2: Normal entities - full visibility check
        _world->system<Modules::Base::Transform, Modules::Base::Streamer, Modules::Base::Streamable>("StreamEntities")
            .kind(flecs::PostUpdate)
            .interval(cfg.tickInterval)
            .run([this](flecs::iter &it) {
                while (it.next()) {
                    const auto tr = it.field<Modules::Base::Transform>(0);
                    const auto s = it.field<Modules::Base::Streamer>(1);
                    const auto rs = it.field<Modules::Base::Streamable>(2);

                    for (auto i : it) {
                        auto streamerEntity = it.entity(i);

                        // Skip streamer entities we plan to remove.
                        if (streamerEntity.get<Modules::Base::PendingRemoval>() != nullptr)
                            continue;

                        // Helper lambda to handle entity updates for already-streaming entities
                        auto handleEntityUpdate = [&](flecs::entity e, Modules::Base::Transform &otherTr, Modules::Base::Streamable &otherS) {
                            // Skip entities that don't want tick updates
                            if (e.has<Modules::Base::NoTickUpdates>())
                                return;

                            auto* streamData = e.get_mut<Modules::Base::StreamedTo>(streamerEntity);
                            if (!streamData)
                                return;

                            const double now = static_cast<double>(Utils::Time::GetTime());
                            if (now - streamData->lastUpdate < otherS.updateInterval)
                                return;

                            streamData->lastUpdate = now;

                            // Send update based on ownership
                            if (otherS.owner == s[i].guid) {
                                // Entity is owned by this streamer - send owner update
                                Networking::Messages::GameSyncEntityOwnerUpdate ownerUpdate;
                                ownerUpdate.FromParameters(otherS.owner);
                                ownerUpdate.SetServerID(e.id());
                                _networkPeer->Send(ownerUpdate, s[i].guid);
                            }
                            else {
                                // Non-owner - send full transform update
                                Networking::Messages::GameSyncEntityUpdate entityUpdate;
                                entityUpdate.FromParameters(otherTr, otherS.owner);
                                entityUpdate.SetServerID(e.id());
                                _networkPeer->Send(entityUpdate, s[i].guid);
                            }
                        };

                        // Send self-update for the streamer's own entity
                        if (!streamerEntity.has<Modules::Base::NoTickUpdates>()) {
                            Networking::Messages::GameSyncEntitySelfUpdate selfUpdate;
                            selfUpdate.SetServerID(streamerEntity.id());
                            _networkPeer->Send(selfUpdate, s[i].guid);
                        }

                        // Phase 1: AlwaysVisible entities - skip distance check, always visible
                        // Uses pre-filtered query (excludes PendingRemoval, Hidden)
                        // Note: AlwaysVisible is a zero-size tag, filtered by query but not passed to callback
                        _alwaysVisibleEntities.each([&](flecs::entity e, Modules::Base::Transform &otherTr, Modules::Base::Streamable &otherS) {
                            if (!e.is_alive() || e == streamerEntity)
                                return;

                            // Only check virtual world (skip distance/custom visibility)
                            if (!AreInSameVirtualWorld(streamerEntity, e))
                                return;

                            const auto isStreaming = e.has<Modules::Base::StreamedTo>(streamerEntity);
                            if (!isStreaming) {
                                Modules::Base::StreamedTo streamData;
                                streamData.lastUpdate = static_cast<double>(Utils::Time::GetTime());
                                e.set<Modules::Base::StreamedTo>(streamerEntity, streamData);
                            } else {
                                // Already streaming - send update if interval passed
                                handleEntityUpdate(e, otherTr, otherS);
                            }
                        });

                        // Phase 2: Normal entities - full visibility check
                        // Uses pre-filtered query (excludes PendingRemoval, Hidden, AlwaysVisible)
                        _normalStreamableEntities.each([&](flecs::entity e, Modules::Base::Transform &otherTr, Modules::Base::Streamable &otherS) {
                            if (!e.is_alive() || e == streamerEntity)
                                return;

                            // Full visibility check for normal entities
                            const auto canSend = this->IsEntityVisibleToStreamer(streamerEntity, e, tr[i], s[i], rs[i], otherTr, otherS);
                            const auto isStreaming = e.has<Modules::Base::StreamedTo>(streamerEntity);

                            if (isStreaming) {
                                if (!canSend) {
                                    e.remove<Modules::Base::StreamedTo>(streamerEntity);
                                } else {
                                    // Still visible - send update if interval passed
                                    handleEntityUpdate(e, otherTr, otherS);
                                }
                            } else if (canSend) {
                                Modules::Base::StreamedTo streamData;
                                streamData.lastUpdate = static_cast<double>(Utils::Time::GetTime());
                                e.set<Modules::Base::StreamedTo>(streamerEntity, streamData);
                            }
                        });
                    }
                }
            });

        return EngineError::ENGINE_NONE;
    }

    EngineError ServerEngine::Shutdown() {
        return Engine::Shutdown();
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
        const auto es = e.get_mut<Framework::World::Modules::Base::Streamable>();
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

    void ServerEngine::SetOwnerRelation(flecs::entity e, flecs::entity owner) {
        // Remove any existing ownership
        e.remove<Modules::Base::OwnedBy>(flecs::Wildcard);
        // Add new ownership
        if (owner.is_valid() && owner.is_alive()) {
            e.add<Modules::Base::OwnedBy>(owner);
        }
    }

    flecs::entity ServerEngine::GetOwnerRelation(flecs::entity e) const {
        flecs::entity owner = flecs::entity::null();
        e.each<Modules::Base::OwnedBy>([&owner](flecs::entity target) {
            owner = target;
        });
        return owner;
    }

    bool ServerEngine::IsOwnedBy(flecs::entity e, flecs::entity owner) {
        return e.has<Modules::Base::OwnedBy>(owner);
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
        if (e.get<Modules::Base::PendingRemoval>() != nullptr)
            return false;

        // Allow user to override visibility rules completely.
        if (rhsS.isVisibleProc && e.has<Modules::Base::VisibilityReplace>()) {
            return rhsS.isVisibleProc(streamerEntity, e);
        }

        // Mark this entity as visited to prevent infinite recursion in cyclic dependencies.
        if (!visited.insert(e.id()).second) {
            // Already visited - we're in a cycle, skip dependent check for this entity.
            // Continue with the remaining visibility checks.
        }
        else {
            // Check our dependents, if any of them are visible, we are visible as well.
            for (const auto &dependentEntity : rhsS.dependentEntities) {
                if (!dependentEntity.is_valid() || !dependentEntity.is_alive())
                    continue;
                if (e == dependentEntity)
                    continue;
                // Skip if already visited (part of a cycle)
                if (visited.find(dependentEntity.id()) != visited.end())
                    continue;
                const auto &dependentS  = dependentEntity.get<Modules::Base::Streamable>();
                const auto &dependentTr = dependentEntity.get<Modules::Base::Transform>();
                if (!dependentS || !dependentTr)
                    continue;
                if (IsEntityVisibleToStreamerInternal(streamerEntity, dependentEntity, lhsTr, streamer, lhsS, *dependentTr, *dependentS, visited)) {
                    return true;
                }
            }
        }

        // Entity is always visible to clients.
        if (e.has<Modules::Base::AlwaysVisible>())
            return true;

        // Entity can be hidden from clients.
        if (e.has<Modules::Base::Hidden>())
            return false;

        // Validate if the entity resides in the same virtual world client does.
        if (!AreInSameVirtualWorld(streamerEntity, e))
            return false;

        // Let user replace the distance check.
        if (rhsS.isVisibleProc && e.has<Modules::Base::VisibilityReplacePosition>()) {
            return rhsS.isVisibleProc(streamerEntity, e);
        }

        // Perform distance check.
        const auto dist = glm::distance(lhsTr.pos, rhsTr.pos);
        auto isVisible  = dist < streamer.range;

        // If we made it this far and the entity is streaming range check exempt
        // we override isVisible state to True.
        if (streamer.rangeExemptEntities.find(e.id()) != streamer.rangeExemptEntities.end()) {
            isVisible = true;
        }

        // Allow user to provide additional rules for visibility.
        // ADD mode is default when neither Replace tag is present
        if (rhsS.isVisibleProc && !e.has<Modules::Base::VisibilityReplace>() && !e.has<Modules::Base::VisibilityReplacePosition>()) {
            isVisible = isVisible && rhsS.isVisibleProc(streamerEntity, e);
        }

        return isVisible;
    }
} // namespace Framework::World
