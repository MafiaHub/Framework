/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "utils/time.h"
#include "world/modules/base.hpp"
#include "world/modules/network.hpp"

#include <flecs/addons/timer.h>
#include <flecs/flecs.h>
#include <functional>
#include <glm/ext.hpp>
#include <slikenet/types.h>
#include <unordered_map>

namespace Framework::Integrations::Shared::Modules {
    struct Server {
        flecs::entity GetStreamer() {
            return _streamEntities;
        }

        Server(flecs::world &world) {
            world.module<Server>();
            using namespace Framework::World;
            using namespace Framework::World::Modules;

            auto allStreamableEntities = world.query_builder<Base::Transform, Network::Streamable>().build();

            auto isVisible = [](Base::Transform &lhsTr, Network::Streamer &streamer, Network::Streamable &lhsS, Base::Transform &rhsTr, Network::Streamable rhsS) -> bool {
                if (rhsS.alwaysVisible)
                    return true;
                if (!rhsS.isVisible)
                    return false;
                if (lhsS.virtualWorld != rhsS.virtualWorld)
                    return false;

                const auto dist = glm::distance(lhsTr.pos, rhsTr.pos);
                return dist < streamer.range;
            };

            float tickDelay = 7.813f;

            _streamEntities = world.system<Base::Transform, Network::Streamer, Network::Streamable>("StreamEntities")
                                  .kind(flecs::PostUpdate)
                                  .interval(tickDelay)
                                  .iter([allStreamableEntities, isVisible](flecs::iter it, Base::Transform *tr, Network::Streamer *s, Network::Streamable *rs) {
                                      for (size_t i = 0; i < it.count(); i++) {
                                          allStreamableEntities.each([=](flecs::entity e, Base::Transform &otherTr, Network::Streamable &otherS) {
                                              if (e == it.entity(i)) {
                                                  // Skip ourselves, we handle such update separately
                                                  return;
                                              }
                                              const auto canSend = isVisible(tr[i], s[i], rs[i], otherTr, otherS);
                                              const auto id      = e.id();
                                              const auto map_it  = s[i].entities.find(id);
                                              if (map_it != s[i].entities.end()) {
                                                  // If we can't stream an entity anymore, de-spawn it
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
                                                      Network::Streamer::StreamData data;
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
} // namespace Framework::Integrations::Shared::Modules
