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

        template <typename... Args>
        inline flecs::entity Create(Args &&...args) {
            auto e = _world->entity<Args...>(std::forward<Args>(args)...);

            AssignPlayerComponents(e);
            return e;
        }
    };
} // namespace Framework::World::Archetypes
