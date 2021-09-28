#pragma once

#include "../modules/base.hpp"
#include "../modules/network.hpp"

#include <flecs/flecs.h>

namespace Framework::World::Archetypes {
    class PlayerFactory {
      private:
        flecs::world *_world = nullptr;

        inline void AssignPlayerComponents(flecs::entity &e) {
            e.add<Modules::Base::Transform>();
            e.add<Modules::Base::Frame>();
            e.add<Modules::Network::Streamable>();
            e.add<Modules::Network::Streamer>();
        }

      public:
        PlayerFactory(flecs::world *world): _world(world) {}

        inline flecs::entity Create(char *name, SLNet::RakNetGUID guid) {
            auto e = _world->entity(name);

            AssignPlayerComponents(e);

            auto s = e.get_mut<Modules::Network::Streamer>();
            s->peer = guid;
            return e;
        }

        /* entity wrappers */
        /* useful in client code */

        inline flecs::entity Create(flecs::entity_t id) {
            auto e = _world->entity(id);

            AssignPlayerComponents(e);
            return e;
        }
    };
} // namespace Framework::World::Archetype
