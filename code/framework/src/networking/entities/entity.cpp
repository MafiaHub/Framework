/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "entity.h"

namespace Framework::Networking::Entities {
    Entity::Entity(): _owner(0), _type(EntityType::NONE), _virtualWorld(0) {}

    Entity::Entity(SLNet::RakNetGUID owner, EntityType type, int world): _owner(owner), _type(type), _virtualWorld(world) {}
} // namespace Framework::Networking::Entities
