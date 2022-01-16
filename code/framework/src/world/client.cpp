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
        const auto status = Engine::Init();

        if (status != EngineError::ENGINE_NONE) {
            return status;
        }

        return EngineError::ENGINE_NONE;
    }

    EngineError ClientEngine::Shutdown() {
        return Engine::Shutdown();
    }

    void ClientEngine::Update() {
        Engine::Update();
    }

    void ClientEngine::OnConnect(Networking::NetworkPeer *peer, float tickInterval) {
        _peer = peer;

        _streamEntities = _world->system<Modules::Base::Transform, Modules::Base::Streamer, Modules::Base::Streamable>("StreamEntities")
                              .kind(flecs::PostUpdate)
                              .interval(tickInterval)
                              .iter([this](flecs::iter it, Modules::Base::Transform *tr, Modules::Base::Streamer *s, Modules::Base::Streamable *rs) {
                                  const auto myGUID = _peer->GetPeer()->GetMyGUID();

                                  for (size_t i = 0; i < it.count(); i++) {
                                      const auto &es = &rs[i];

                                      if (es->owner == myGUID.g) {
                                          if (es->events.updateProc) {
                                              es->events.updateProc(_peer, (SLNet::UNASSIGNED_RAKNET_GUID).g, it.entity(i));
                                          }
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
