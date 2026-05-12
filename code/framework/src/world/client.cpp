/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "client.h"

#include "game_rpc/set_frame.h"
#include "game_rpc/set_transform.h"

namespace Framework::World {
    WorldError ClientEngine::Init() {
        if (Engine::Init(nullptr) != WorldError::WORLD_NONE) { // assigned by OnConnect
            return WorldError::WORLD_FLECS_INIT_FAILED;
        }

        // Translate StreamUpdateEvent into outbound network updates on the
        // client. Mods can layer further behavior by subscribing to the same
        // custom events.
        Modules::Base::RegisterClientStreamObservers(*_world);

        // Observers maintain the serverID -> entity cache so lookups are O(1)
        // and we no longer scan all ServerID-bearing entities on every query.
        _world->observer<Modules::Base::ServerID>()
            .event(flecs::OnSet)
            .each([this](flecs::entity e, Modules::Base::ServerID &sid) {
                // Drop the stale forward entry if this entity's serverID was
                // reassigned — OnSet only sees the new value.
                auto prev = _entityToServerID.find(e.id());
                if (prev != _entityToServerID.end() && prev->second != sid.id) {
                    auto fwd = _serverIDToEntity.find(prev->second);
                    if (fwd != _serverIDToEntity.end() && fwd->second == e.id()) {
                        _serverIDToEntity.erase(fwd);
                    }
                }
                _serverIDToEntity[sid.id]  = e.id();
                _entityToServerID[e.id()] = sid.id;
            });

        _world->observer<Modules::Base::ServerID>()
            .event(flecs::OnRemove)
            .each([this](flecs::entity e, Modules::Base::ServerID &sid) {
                const auto it = _serverIDToEntity.find(sid.id);
                if (it != _serverIDToEntity.end() && it->second == e.id()) {
                    _serverIDToEntity.erase(it);
                }
                _entityToServerID.erase(e.id());
            });

        return WorldError::WORLD_NONE;
    }

    void ClientEngine::Shutdown() {
        Engine::Shutdown();
    }

    void ClientEngine::Update() {
        Engine::Update();
    }

    flecs::entity ClientEngine::GetEntityByServerID(flecs::entity_t id) const {
        const auto it = _serverIDToEntity.find(id);
        if (it == _serverIDToEntity.end()) {
            return flecs::entity::null();
        }
        flecs::entity e(_world->get_world(), it->second);
        if (!e.is_alive()) {
            return flecs::entity::null();
        }
        return e;
    }

    flecs::entity_t ClientEngine::GetServerID(flecs::entity entity) {
        if (!entity.is_alive()) {
            return 0;
        }

        if(const auto serverID = entity.try_get<Modules::Base::ServerID>())
            return serverID->id;
        return 0;
    }

    flecs::entity ClientEngine::CreateEntity(flecs::entity_t serverID) const {
        const auto e = _world->entity();

        // Use set<> so the OnSet observer fires and the serverID cache is
        // populated immediately. ensure<>() would only emit OnAdd.
        e.set<Modules::Base::ServerID>({serverID});
        return e;
    }

    void ClientEngine::OnConnect(Networking::NetworkPeer *peer, float tickInterval) {
        SetNetworkPeer(peer);

        _streamEntities = _world->system<Modules::Base::Transform, Modules::Base::Streamable>("StreamEntities").kind(flecs::PostUpdate).interval(tickInterval).run([this](flecs::iter &it) {
            const auto peer = GetNetworkPeer();
            if (!peer) return;
            const auto myGUID = peer->GetPeer()->GetMyGUID();

            while (it.next()) {
                const auto rs = it.field<Modules::Base::Streamable>(1);
                for (auto i : it) {
                    const auto e = it.entity(i);
                    if (!rs[i].performTickUpdates) continue;
                    if (!Framework::World::Engine::IsEntityOwner(e, myGUID.g)) continue;

                    _world->event<Modules::Base::StreamUpdateEvent>()
                        .id<Modules::Base::Streamable>()
                        .entity(e)
                        .ctx(Modules::Base::StreamUpdateEvent{{peer, SLNet::UNASSIGNED_RAKNET_GUID.g}})
                        .emit();
                }
            }
        });

        // Register built-in RPCs
        InitRPCs(peer);
    }

    void ClientEngine::OnDisconnect() {
        if (_streamEntities.is_alive()) {
            _streamEntities.destruct();
        }

        _world->defer([&] {
            _allStreamableEntities.each([this](flecs::entity e, Modules::Base::Transform &, Modules::Base::Streamable &str) {
                if (_onEntityDestroyCallback && !_onEntityDestroyCallback(e))
                    return;
                if (str.modEvents.disconnectProc)
                    str.modEvents.disconnectProc(e);
                e.destruct();
            });
        });

        SetNetworkPeer(nullptr);
    }
    void ClientEngine::InitRPCs(Networking::NetworkPeer *net) const {
        net->RegisterGameRPC<RPC::SetTransform>([this](SLNet::RakNetGUID guid, RPC::SetTransform *msg) {
            if (!msg->Valid()) {
                return;
            }
            const auto e = GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }
            // Peer-controlled message: drop it if the target entity lacks the
            // component instead of dereferencing a null pointer.
            const auto tr = e.try_get_mut<World::Modules::Base::Transform>();
            if (!tr)
                return;
            *tr = msg->GetTransform();
            e.modified<World::Modules::Base::Transform>();
        });
        net->RegisterGameRPC<RPC::SetFrame>([this](SLNet::RakNetGUID guid, RPC::SetFrame *msg) {
            if (!msg->Valid()) {
                return;
            }
            const auto e = GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }
            const auto fr = e.try_get_mut<World::Modules::Base::Frame>();
            if (!fr)
                return;
            *fr = msg->GetFrame();
            e.modified<World::Modules::Base::Frame>();
        });
    }

    void ClientEngine::UpdateEntityTransform(flecs::entity entity, const Modules::Base::Transform &rhs) {
        if (!entity.is_valid() || !entity.is_alive()) {
            return;
        }

        auto tr = entity.try_get_mut<Modules::Base::Transform>();
        if (!tr)
            return;
        *tr = rhs;

        // Streamable is optional on the entity — only fire the mod hook when
        // the component is actually present.
        if (const auto str = entity.try_get_mut<Modules::Base::Streamable>()) {
            if (str->modEvents.updateTransformProc) {
                str->modEvents.updateTransformProc(entity);
            }
        }
        entity.modified<World::Modules::Base::Transform>();
    }
} // namespace Framework::World
