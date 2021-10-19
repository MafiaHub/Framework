/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "server.h"

#include "utils/time.h"

namespace Framework::World {
    EngineError ServerEngine::Init() {
        const auto status = Engine::Init();

        if (status != EngineError::ENGINE_NONE) {
            return status;
        }

        auto allStreamableEntities = _world->query_builder<Modules::Base::Transform, Modules::Base::Streamable>().build();

        auto isVisible = [](flecs::entity e, Modules::Base::Transform &lhsTr, Modules::Base::Streamer &streamer, Modules::Base::Streamable &lhsS, Modules::Base::Transform &rhsTr,
                             Modules::Base::Streamable rhsS) -> bool {
            if (e.get<Modules::Base::PendingRemoval>() != nullptr)
                return false;
            if (rhsS.alwaysVisible)
                return true;
            if (!rhsS.isVisible)
                return false;
            if (lhsS.virtualWorld != rhsS.virtualWorld)
                return false;

            const auto dist = glm::distance(lhsTr.pos, rhsTr.pos);
            return dist < streamer.range;
        };

        _streamEntities = _world->system<Modules::Base::Transform, Modules::Base::Streamer, Modules::Base::Streamable>("StreamEntities")
                              .kind(flecs::PostUpdate)
                              .interval(_tickDelay)
                              .iter([allStreamableEntities, isVisible](flecs::iter it, Modules::Base::Transform *tr, Modules::Base::Streamer *s, Modules::Base::Streamable *rs) {
                                  for (size_t i = 0; i < it.count(); i++) {
                                      allStreamableEntities.each([=](flecs::entity e, Modules::Base::Transform &otherTr, Modules::Base::Streamable &otherS) {
                                          if (e == it.entity(i) && rs[i].events.selfUpdateProc) {
                                              rs[i].events.selfUpdateProc(s[i].guid, e);
                                              return;
                                          }
                                          const auto id      = e.id();
                                          const auto canSend = isVisible(e, tr[i], s[i], rs[i], otherTr, otherS);
                                          const auto map_it  = s[i].entities.find(id);
                                          if (map_it != s[i].entities.end()) {
                                              // If we can't stream an entity anymore, despawn it
                                              if (!canSend) {
                                                  s[i].entities.erase(map_it);
                                                  if (otherS.events.despawnProc)
                                                      otherS.events.despawnProc(s[i].guid, e);
                                              }

                                              // otherwise we do regular updates
                                              else if (rs[i].owner != otherS.owner) {
                                                  auto &data = map_it->second;
                                                  if (Utils::Time::GetTime() - data.lastUpdate > otherS.updateFrequency) {
                                                      if (otherS.events.updateProc)
                                                          otherS.events.updateProc(s[i].guid, e);
                                                      data.lastUpdate = Utils::Time::GetTime();
                                                  }
                                              }
                                          }

                                          // this is a new entity, spawn it unless user says otherwise
                                          else if (canSend && otherS.events.spawnProc) {
                                              if (otherS.events.spawnProc(s[i].guid, e)) {
                                                  Modules::Base::Streamer::StreamData data;
                                                  data.lastUpdate   = Utils::Time::GetTime();
                                                  s[i].entities[id] = data;
                                              }
                                          }
                                      });
                                  }
                              });

        _world->system<Modules::Base::PendingRemoval>("RemoveEntities")
            .kind(flecs::PostUpdate)
            .interval(_tickDelay * 4.0f)
            .each([](flecs::entity e, Modules::Base::PendingRemoval &pd) {
                e.destruct();
            });

        return EngineError::ENGINE_NONE;
    }

    EngineError ServerEngine::Shutdown() {
        return Engine::Shutdown();
    }

    void ServerEngine::Update() {
        Engine::Update();
    }
} // namespace Framework::World
