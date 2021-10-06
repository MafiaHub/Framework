/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "base.hpp"
#include "utils/time.h"

#include <flecs/addons/timer.h>
#include <flecs/flecs.h>
#include <functional>
#include <glm/ext.hpp>
#include <slikenet/types.h>
#include <unordered_map>

namespace Framework::World::Modules {
    struct Network {
        struct Streamable {
            int virtualWorld = 0;
            bool isVisible;
            int64_t updateFrequency = 1667;
            flecs::entity_t owner   = 0;

            struct Events {
                using Proc = std::function<bool(SLNet::RakNetGUID, flecs::entity &)>;
                Proc spawnProc;
                Proc despawnProc;
                Proc selfUpdateProc;
                Proc updateProc;
            } events;
        };

        struct Streamer {
            struct StreamData {
                int64_t lastUpdate = 0;
            };
            std::unordered_map<flecs::entity_t, StreamData> entities;
            float range = 100.0f;

            SLNet::RakNetGUID peer = SLNet::UNASSIGNED_RAKNET_GUID;
        };

        flecs::entity GetStreamer() {
            return _streamEntities;
        }

        Network(flecs::world &world) {
            world.module<Network>();

            world.component<Streamable>();
            world.component<Streamer>();

            auto allStreamableEntities = world.query_builder<Base::Transform, Streamable>().term<Streamer>().oper(flecs::Not).build();

            auto isVisible = [](Base::Transform &lhsTr, Streamer &streamer, Streamable &lhsS, Base::Transform &rhsTr, Streamable rhsS) -> bool {
                if (!rhsS.isVisible)
                    return false;
                if (lhsS.virtualWorld != rhsS.virtualWorld)
                    return false;

                const auto dist = glm::distance(lhsTr.pos, rhsTr.pos);
                return dist < streamer.range;
            };

            float tickDelay = 7.813f;

            _streamEntities = world.system<Base::Transform, Streamer, Streamable>("StreamEntities")
                                  .kind(flecs::PostUpdate)
                                  .interval(tickDelay)
                                  .iter([allStreamableEntities, isVisible](flecs::iter it, Base::Transform *tr, Streamer *s, Streamable *rs) {
                                      for (size_t i = 0; i < it.count(); i++) {
                                          allStreamableEntities.each([=](flecs::entity e, Base::Transform &otherTr, Streamable &otherS) {
                                              const auto canSend = isVisible(tr[i], s[i], rs[i], otherTr, otherS);
                                              const auto id      = e.id();
                                              const auto map_it  = s[i].entities.find(id);
                                              if (map_it != s[i].entities.end()) {
                                                  // If we can't stream an entity anymore, despawn it
                                                  if (!canSend) {
                                                      s[i].entities.erase(map_it);
                                                      if (otherS.events.despawnProc)
                                                          otherS.events.despawnProc(s[i].peer, e);
                                                  }

                                                  // otherwise we do regular updates
                                                  else if (rs[i].owner != otherS.owner) {
                                                      auto &data = map_it->second;
                                                      if (Utils::Time::GetTime() - data.lastUpdate > otherS.updateFrequency) {
                                                          if (otherS.events.updateProc)
                                                              otherS.events.updateProc(s[i].peer, e);
                                                          data.lastUpdate = Utils::Time::GetTime();
                                                      }
                                                  }
                                              }

                                              // this is a new entity, spawn it unless user says otherwise
                                              else if (canSend && otherS.events.spawnProc) {
                                                  if (otherS.events.spawnProc(s[i].peer, e)) {
                                                      Streamer::StreamData data;
                                                      data.lastUpdate   = Utils::Time::GetTime();
                                                      s[i].entities[id] = data;
                                                  }
                                              }
                                          });

                                          if (rs[i].events.selfUpdateProc)
                                              rs[i].events.selfUpdateProc(s[i].peer, it.entity(i));
                                      }
                                  });
        }

        flecs::entity _streamEntities;
    };
} // namespace Framework::World::Modules
