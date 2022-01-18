/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "client.h"

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

    flecs::entity ClientEngine::GetEntityByServerID(flecs::entity_t id) {
        flecs::entity ent = {};
        _queryGetEntityByServerID.iter([&ent, id](flecs::iter it, Modules::Base::ServerID *rhs) {
            for (size_t i = 0; i < it.count(); i++) {
                if (id == rhs[i].id) {
                    ent = it.entity(i);
                    return;
                }
            }
        });
        return ent;
    }

    flecs::entity ClientEngine::CreateEntity(flecs::entity_t serverID) {
        const auto e = _world->entity();

        auto sid = e.get_mut<Modules::Base::ServerID>();
        sid->id  = serverID;
        return e;
    }

    void ClientEngine::OnConnect(Networking::NetworkPeer *peer, float tickInterval) {
        _networkPeer = peer;

        _streamEntities = _world->system<Modules::Base::Transform, Modules::Base::Streamer, Modules::Base::Streamable>("StreamEntities")
                              .kind(flecs::PostUpdate)
                              .interval(tickInterval)
                              .iter([this](flecs::iter it, Modules::Base::Transform *tr, Modules::Base::Streamer *s, Modules::Base::Streamable *rs) {
                                  const auto myGUID = _networkPeer->GetPeer()->GetMyGUID();

                                  for (size_t i = 0; i < it.count(); i++) {
                                      const auto &es = &rs[i];

                                      if (es->events.updateProc && es->owner == myGUID.g) {
                                          es->events.updateProc(_networkPeer, (SLNet::UNASSIGNED_RAKNET_GUID).g, it.entity(i));
                                      }
                                  }
                              });
    }

    void ClientEngine::OnDisconnect() {
        if (_streamEntities.is_alive()) {
            _streamEntities.destruct();
        }
    }

} // namespace Framework::World
