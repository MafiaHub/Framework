/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "network_entity.h"

#include <function2.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Framework::Networking::Replication {
    // Process-wide registry mapping a stable type id (CRC32 of a name) to a constructor for the
    // concrete NetworkEntity subclass. The server creates entities by type id and the client's
    // AllocReplica reconstructs them from the same id, so both sides must register identical types.
    class EntityFactory final {
      public:
        using Constructor = fu2::function<NetworkEntity *() const>;

        static EntityFactory &Get();

        // Registers a type and returns its id. Not thread-safe: register every type at startup,
        // before networking begins, because Create() is then called from the sim/network path.
        uint32_t Register(const std::string &name, Constructor constructor);

        // Constructs an instance and stamps its typeId. Returns nullptr for an unknown id.
        NetworkEntity *Create(uint32_t typeId) const;

        uint32_t TypeId(const std::string &name) const;

      private:
        struct Entry {
            uint32_t id = 0;
            Constructor constructor;
        };
        std::unordered_map<uint32_t, Entry> _types;
    };
} // namespace Framework::Networking::Replication
