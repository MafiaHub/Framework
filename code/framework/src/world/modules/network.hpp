#pragma once

#include "base.hpp"
#include "utils/time.h"

#include <flecs/flecs.h>
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
        };

        struct Streamer {
            struct StreamData {
                int64_t lastUpdate = 0;
            };
            std::unordered_map<flecs::entity_t, StreamData> entities;
            float range = 100.0f;

            SLNet::RakNetGUID peer = SLNet::UNASSIGNED_RAKNET_GUID;
        };

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

            world.system<Base::Transform, Streamer, Streamable>("StreamEntities")
                .kind(flecs::PostUpdate)
                .iter([allStreamableEntities, isVisible](flecs::iter it, Base::Transform *tr, Streamer *s, Streamable *rs) {
                    for (size_t i = 0; i < it.count(); i++) {
                        allStreamableEntities.each([=](flecs::entity e, Base::Transform &otherTr, Streamable &otherS) {
                            const auto canSend = isVisible(tr[i], s[i], rs[i], otherTr, otherS);
                            const auto id      = e.id();
                            const auto map_it  = s[i].entities.find(id);
                            if (map_it != s[i].entities.end()) {
                                if (!canSend) {
                                    s[i].entities.erase(map_it);
                                    // TODO: send despawn
                                }
                                else if (rs[i].owner != otherS.owner) {
                                    // TODO: regulate throughput and send update
                                }
                            }
                            else if (canSend) {
                                Streamer::StreamData data;
                                data.lastUpdate   = Utils::Time::GetTime();
                                s[i].entities[id] = data;
                                // TODO: send spawn
                            }
                        });

                        // TODO: send self-update to streamer
                    }
                });
        }
    };
} // namespace Framework::World::Modules
