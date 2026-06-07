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
#include <utility>

namespace Framework::Networking::Replication {
    // Process-wide registry mapping a stable type id (CRC32 of a name) to a constructor for the
    // concrete NetworkEntity subclass. The server creates entities by type id and the client's
    // AllocReplica reconstructs them from the same id, so both sides must register identical types.
    class EntityFactory final {
      public:
        // The trailing `const` is required, not cosmetic: Create() is a const method and invokes the
        // stored constructor through the const _types map, so the callable must be const-invocable.
        using Constructor = fu2::function<NetworkEntity *() const>;

        static EntityFactory &Get();

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

        uint32_t TypeId(const std::string &name) const;

      private:
        struct Entry {
            uint32_t id = 0;
            std::string name; // kept for collision diagnostics
            Constructor constructor;
        };
        std::unordered_map<uint32_t, Entry> _types;
    };

    template <typename T>
    inline uint32_t EntityFactory::Register(const std::string &name) {
        return Register(name, [] {
            return new T();
        });
    }

    // Registers a NetworkEntity subclass with the global EntityFactory at static-init time — the same
    // self-registration pattern as MafiaNet::RPC4GlobalRegistration. Use the FW_REGISTER_NETWORK_ENTITY
    // macro rather than instantiating this directly.
    template <typename T>
    struct EntityRegistration final {
        explicit EntityRegistration(const std::string &name) {
            EntityFactory::Get().Register<T>(name);
        }
    };
} // namespace Framework::Networking::Replication

// Register a NetworkEntity subclass under a stable wire name (CRC32'd into its type id). Put it at
// file scope in the subclass's .cpp; the identical name must be registered on both client and
// server. TYPE must be a simple class identifier. Example:
//   FW_REGISTER_NETWORK_ENTITY(Player, "Player");
#define FW_REGISTER_NETWORK_ENTITY(TYPE, NAME)                                                       \
    namespace {                                                                                       \
        const ::Framework::Networking::Replication::EntityRegistration<TYPE> TYPE##_fwEntityRegistration {(NAME)}; \
    }
