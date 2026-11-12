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

#include "../class_cache.h"

#include <core_modules.h>
#include <networking/replication/network_entity.h>
#include <networking/replication/replication_manager.h>
#include <networking/replication/world.h>

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Framework::Scripting::Builtins {
    // Base scripting handle for a replicated entity, reusable across mods. Wraps the entity's
    // NetworkID and resolves the live NetworkEntity on demand through the replication world, so a
    // stale JS handle to a destroyed entity resolves to nothing instead of dangling. What Resolve
    // gives holds the entity's lock for the scope that keeps it: take it, use the entity, let go
    // before calling into script or resolving another. Exposes the common transform
    // (position/rotation); writing it goes through NetworkEntity::ForceState, so the server's value is
    // authoritative even over an entity a client owns. Mods derive their own handles (player, vehicle)
    // via v8pp inherit<Entity>() and add their game-specific properties.
    class Entity {
      public:
        // Wraps the id and nothing else: no entity is asked for, so one may be built while a handle is
        // held (an event argument made inside a handler) and for an id whose entity is already gone.
        // Every method resolves when it is called and answers with nothing for a stale id.
        Entity(uint64_t networkId): _id(networkId) {}
        virtual ~Entity() = default;

        uint64_t GetId() const {
            return _id;
        }

        using Held = Networking::Replication::Ref<Networking::Replication::NetworkEntity>;

        Held GetHandle() const {
            return Resolve();
        }

        // The connection that owns the entity. False when the handle is stale; an entity the server
        // owns answers true with the unassigned guid.
        bool ResolveOwner(MafiaNet::PeerGuid &owner) const;

        Vector3 GetPosition() const;
        void SetPosition(const Vector3 &pos);

        Quaternion GetRotation() const;
        void SetRotationFromEuler(const Vector3 &rot);
        void SetRotationFromQuaternion(const Quaternion &quat);

        uint32_t GetVirtualWorld() const;
        void SetVirtualWorld(uint32_t world);

        // Restrict streaming to a single player's connection (unassigned clears). Range/visibility
        // still apply.
        void SetVisibleTo(MafiaNet::PeerGuid target);

        virtual std::string ToString() const;

        static v8pp::class_<Entity> &GetClass(v8::Isolate *isolate);

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);

        // What `new T(id)` runs in a script, for T an Entity or a handle derived from it: unlike a
        // wrapper made natively, it refuses an id that names no entity, since a script that types one
        // in should hear that it is wrong. Asks the world's table of ids only (World::Exists).
        // Throws std::runtime_error, which v8pp hands the script.
        template <typename T>
        static T *ConstructFromScript(const v8::FunctionCallbackInfo<v8::Value> &args) {
            const uint64_t id = args.Length() > 0 ? v8pp::from_v8<uint64_t>(args.GetIsolate(), args[0]) : 0;
            RequireExists(id);
            return new T(id);
        }

      protected:
        static void RequireExists(uint64_t networkId);
        Held Resolve() const;
        // The same as a concrete type, empty if the entity is of another.
        template <typename T>
        Networking::Replication::Ref<T> ResolveAs() const {
            auto *replication = CoreModules::GetReplication();
            return replication ? replication->GetWorld().Write<T>(_id) : Networking::Replication::Ref<T>();
        }

        uint64_t _id = 0;
        inline static ClassCache<Entity> _classes;
    };
} // namespace Framework::Scripting::Builtins
