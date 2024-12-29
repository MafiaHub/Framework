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
    EngineError ClientEngine::Init() {
        const auto status = Engine::Init(nullptr); // assigned by OnConnect

        if (status != EngineError::ENGINE_NONE) {
            return status;
        }

        _queryGetEntityByServerID = _world->query_builder<Modules::Base::ServerID>().build();

        return EngineError::ENGINE_NONE;
    }

    EngineError ClientEngine::Shutdown() {
        return Engine::Shutdown();
    }

    void ClientEngine::Update() {
        Engine::Update();
    }

    flecs::entity ClientEngine::GetEntityByServerID(flecs::entity_t id) const {
        flecs::entity ent = {};
        _queryGetEntityByServerID.each([&ent, id](flecs::entity e, Modules::Base::ServerID& rhs) {
            if (id == rhs.id) {
                ent = e;
                return;
            }
        });
        return ent;
    }

    flecs::entity_t ClientEngine::GetServerID(flecs::entity entity) {
        if (!entity.is_alive()) {
            return 0;
        }

        const auto serverID = entity.get<Modules::Base::ServerID>();
        return serverID->id;
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
                    const auto &es = &rs[i];

                    if (es->GetBaseEvents().updateProc && Framework::World::Engine::IsEntityOwner(it.entity(i), myGUID.g)) {
                        es->GetBaseEvents().updateProc(_networkPeer, (SLNet::UNASSIGNED_RAKNET_GUID).g, it.entity(i));
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
        _allStreamableEntities.each([this](flecs::entity e, Modules::Base::Transform&, Modules::Base::Streamable&) {
            if (_onEntityDestroyCallback) {
                if (!_onEntityDestroyCallback(e)) {
                    return;
                }
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

} // namespace Framework::World
