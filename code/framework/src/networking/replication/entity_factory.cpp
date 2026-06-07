/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "entity_factory.h"

#include <logging/logger.h>
#include <utils/hashing.h>

namespace Framework::Networking::Replication {
    EntityFactory &EntityFactory::Get() {
        static EntityFactory instance;
        return instance;
    }

    uint32_t EntityFactory::TypeId(const std::string &name) const {
        return Utils::Hashing::CalculateCRC32(name.c_str());
    }

    uint32_t EntityFactory::Register(const std::string &name, Constructor constructor) {
        const uint32_t id   = TypeId(name);
        const auto existing = _types.find(id);
        // Re-registering the same name is benign (e.g. a second init); a different name on the same id
        // is a CRC32 collision that would silently shadow the first type — surface it loudly.
        if (existing != _types.end() && existing->second.name != name) {
            Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->error("EntityFactory: CRC32 collision — '{}' and '{}' both hash to {}; the latter shadows the former", existing->second.name, name, id);
        }
        _types[id] = Entry {id, name, std::move(constructor)};
        return id;
    }

    NetworkEntity *EntityFactory::Create(uint32_t typeId) const {
        const auto it = _types.find(typeId);
        if (it == _types.end() || !it->second.constructor) {
            return nullptr;
        }
        NetworkEntity *entity = it->second.constructor();
        if (entity) {
            entity->typeId = typeId;
        }
        return entity;
    }
} // namespace Framework::Networking::Replication
