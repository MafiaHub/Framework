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
#include "networking/messages/game_sync/entity_messages.h"

namespace Framework::World {
    EngineError ClientEngine::Init() {
        const auto status = Engine::Init(nullptr); // assigned by OnConnect

        if (status != EngineError::ENGINE_NONE) {
            return status;
        }

        _queryGetEntityByServerID = _world->query_builder<Modules::Base::ServerID>().build();

        // Observer to maintain ServerID cache
        _world->observer<Modules::Base::ServerID>("ServerIDCacheUpdate")
            .event(flecs::OnSet)
            .each([this](flecs::entity e, Modules::Base::ServerID& sid) {
                _serverIdCache[sid.id] = e;
            });

        _world->observer<Modules::Base::ServerID>("ServerIDCacheRemove")
            .event(flecs::OnRemove)
            .each([this](flecs::entity e, Modules::Base::ServerID& sid) {
                _serverIdCache.erase(sid.id);
            });

        return EngineError::ENGINE_NONE;
    }

    EngineError ClientEngine::Shutdown() {
        return Engine::Shutdown();
    }

    void ClientEngine::Update() {
        Engine::Update();
    }

    flecs::entity ClientEngine::GetEntityByServerID(flecs::entity_t id) const {
        auto it = _serverIdCache.find(id);
        if (it != _serverIdCache.end() && it->second.is_alive()) {
            return it->second;
        }
        return flecs::entity::null();
    }

    flecs::entity_t ClientEngine::GetServerID(flecs::entity entity) {
        if (!entity.is_alive()) {
            return 0;
        }

        if(const auto serverID = entity.get<Modules::Base::ServerID>())
            return serverID->id;
        return 0;
    }

    flecs::entity ClientEngine::CreateEntity(flecs::entity_t serverID) const {
        const auto e = _world->entity();

        auto &sid = e.ensure<Modules::Base::ServerID>();
        sid.id        = serverID;
        return e;
    }

    void ClientEngine::OnConnect(Networking::NetworkPeer *peer, float tickInterval) {
        _networkPeer = peer;

        _streamEntities = _world->system<Modules::Base::Transform, Modules::Base::Streamable>("StreamEntities").kind(flecs::PostUpdate).interval(tickInterval).run([this](flecs::iter &it) {
            const auto myGUID = _networkPeer->GetPeer()->GetMyGUID();

            while (it.next()) {
                const auto tr = it.field<Modules::Base::Transform>(0);
                const auto rs = it.field<Modules::Base::Streamable>(1);

                for (auto i : it) {
                    const auto e = it.entity(i);

                    // Send updates for entities we own
                    if (!e.has<Modules::Base::NoTickUpdates>() && Framework::World::Engine::IsEntityOwner(e, myGUID.g)) {
                        const auto sid = e.get<Modules::Base::ServerID>();
                        if (sid) {
                            Framework::Networking::Messages::GameSyncEntityUpdate entityUpdate;
                            entityUpdate.FromParameters(tr[i], 0);
                            entityUpdate.SetServerID(sid->id);
                            _networkPeer->Send(entityUpdate, (SLNet::UNASSIGNED_RAKNET_GUID).g);
                        }
                    }
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

        _world->defer_begin();
        _allStreamableEntities.each([this](flecs::entity e, Modules::Base::Transform&, Modules::Base::Streamable& str) {
            if (_onEntityDestroyCallback) {
                if (!_onEntityDestroyCallback(e)) {
                    return;
                }
            }

            if (str.disconnectProc) {
                str.disconnectProc(e);
            }

            e.destruct();
        });
        _world->defer_end();

        _networkPeer = nullptr;
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
            const auto tr = e.get_mut<World::Modules::Base::Transform>();
            *tr           = msg->GetTransform();
        });
        net->RegisterGameRPC<RPC::SetFrame>([this](SLNet::RakNetGUID guid, RPC::SetFrame *msg) {
            if (!msg->Valid()) {
                return;
            }
            const auto e = GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }
            const auto fr = e.get_mut<World::Modules::Base::Frame>();
            *fr           = msg->GetFrame();
        });
    }

    void ClientEngine::UpdateEntityTransform(flecs::entity entity, Modules::Base::Transform &rhs) {
        if (!entity.is_valid() || !entity.is_alive()) {
            return;
        }

        auto tr = entity.get_mut<Modules::Base::Transform>();
        *tr     = rhs;

        const auto str = entity.get_mut<Modules::Base::Streamable>();
        if (str && str->updateTransformProc) {
            str->updateTransformProc(entity);
        }
    }
} // namespace Framework::World
