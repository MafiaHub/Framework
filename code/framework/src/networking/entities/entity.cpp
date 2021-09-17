#include "entity.h"

namespace Framework::Networking::Entities {
    Entity::Entity(): _owner(0), _type(EntityType::NONE), _virtualWorld(0) {}

    Entity::Entity(SLNet::RakNetGUID owner, EntityType type, int world)
        : _owner(owner)
        , _type(type)
        , _virtualWorld(world) {}
} // namespace Framework::Networking::Entities
