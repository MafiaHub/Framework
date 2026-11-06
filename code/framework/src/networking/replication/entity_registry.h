/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "network_entity.h"

#include <utils/hashing.h>

#include <function2/function2.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace Framework::Networking::Replication {
    // Process-wide registry mapping a stable type id (CRC32 of a name) to a constructor for the
    // concrete NetworkEntity subclass. The server creates entities by type id and the client's
    // AllocReplica reconstructs them from the same id, so both sides must register identical types.
    class EntityRegistry final {
      public:
        // The trailing `const` is required, not cosmetic: Create() is a const method and invokes the
        // stored constructor through the const _types map, so the callable must be const-invocable.
        using Constructor = fu2::function<NetworkEntity *() const>;

        static EntityRegistry &Get();

        // Registers a type and returns its id. Logs if two distinct names collide on the same CRC32
        // (the later one would otherwise silently shadow the former). Not thread-safe: register every
        // type at startup, before networking begins, because Create() runs on the sim/network path.
        uint32_t Register(const std::string &name, Constructor constructor);

        // Convenience overload: default-constructs T. Prefer this — it removes the `[]{ return new T; }`
        // boilerplate at every registration site. Defined out-of-class below.
        template <typename T>
        uint32_t Register(const std::string &name);

        // Constructs an instance and stamps its typeId. Returns nullptr for an unknown id.
        NetworkEntity *Create(uint32_t typeId) const;

        // The wire id of a type name: CRC32 of the bytes. Pure, so it is usable in constant
        // expressions; EntityTypeId<T>() below is the zero-cost form for a known type.
        static constexpr uint32_t TypeId(std::string_view name) {
            return Utils::Hashing::CalculateCRC32(name);
        }

      private:
        struct Entry {
            std::string name; // kept for collision diagnostics
            Constructor constructor;
        };
        std::unordered_map<uint32_t, Entry> _types;
    };

    template <typename T>
    inline uint32_t EntityRegistry::Register(const std::string &name) {
        return Register(name, [] {
            return new T();
        });
    }

    // T's wire id, folded at compile time from T::kTypeName. A subclass that does not declare its
    // own kTypeName inherits its parent's id, which is how a client-side leaf shares the server's.
    template <typename T>
    constexpr uint32_t EntityTypeId() {
        return EntityRegistry::TypeId(std::string_view(T::kTypeName));
    }
} // namespace Framework::Networking::Replication
