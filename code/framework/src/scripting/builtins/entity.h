/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "quaternion.h"
#include "vector3.h"

#include <networking/replication/network_entity.h>
#include <networking/replication/replication_manager.h>

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Framework::Scripting::Builtins {
    // Base scripting handle for a replicated entity, reusable across mods. Wraps the entity's
    // NetworkID and resolves the live NetworkEntity on demand through the world engine, so a stale JS
    // handle to a destroyed entity resolves to null instead of dangling. Exposes the common transform
    // (position/rotation); writing it goes through NetworkEntity::ForceState, so the server's value is
    // authoritative even over an entity a client owns. Mods derive their own handles (player, vehicle)
    // via v8pp inherit<Entity>() and add their game-specific properties.
    class Entity {
      public:
        // Throws std::runtime_error when the network id doesn't resolve to a live entity.
        Entity(uint64_t networkId);
        virtual ~Entity() = default;

        uint64_t GetId() const {
            return _id;
        }

        Networking::Replication::NetworkEntity *GetHandle() const {
            return Resolve();
        }

        Vector3 GetPosition() const;
        void SetPosition(const Vector3 &pos);

        Quaternion GetRotation() const;
        void SetRotationFromEuler(const Vector3 &rot);
        void SetRotationFromQuaternion(const Quaternion &quat);

        uint32_t GetVirtualWorld() const;
        void SetVirtualWorld(uint32_t world);

        // Restrict streaming to a single player's connection (null clears). Range/visibility still apply.
        void SetVisibleTo(Networking::Replication::NetworkEntity *targetEntity);

        virtual std::string ToString() const;

        static v8pp::class_<Entity> &GetClass(v8::Isolate *isolate);

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);

      protected:
        Networking::Replication::NetworkEntity *Resolve() const;

        uint64_t _id = 0;
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Entity>>> _classes;
    };
} // namespace Framework::Scripting::Builtins
