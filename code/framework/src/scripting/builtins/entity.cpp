/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "entity.h"
#include "../lua/lua_runtime.h"
#include "../scripting_catalog.h"
#include "state_bag.h"

#include <core_modules.h>

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

#include <sstream>
#include <stdexcept>

namespace Framework::Scripting::Builtins {
    Entity::Entity(uint64_t networkId): _id(networkId) {
        if (!Resolve()) {
            throw std::runtime_error(fmt::format("Entity handle '{}' is not valid!", networkId));
        }
    }

    Vector3 Entity::GetPosition() const {
        if (auto *e = Resolve()) {
            return Vector3(e->position.x, e->position.y, e->position.z);
        }
        return Vector3(0, 0, 0);
    }

    void Entity::SetPosition(const Vector3 &pos) {
        if (auto *e = Resolve()) {
            e->position = pos.vec();
            e->ForceState();
        }
    }

    Quaternion Entity::GetRotation() const {
        if (auto *e = Resolve()) {
            return Quaternion(e->rotation);
        }
        return Quaternion();
    }

    void Entity::SetRotationFromEuler(const Vector3 &rot) {
        if (auto *e = Resolve()) {
            e->rotation = glm::quat(glm::radians(rot.vec()));
            e->ForceState();
        }
    }

    void Entity::SetRotationFromQuaternion(const Quaternion &quat) {
        if (auto *e = Resolve()) {
            e->rotation = quat.quat();
            e->ForceState();
        }
    }

    uint32_t Entity::GetVirtualWorld() const {
        if (auto *e = Resolve()) {
            return e->GetVirtualWorld();
        }
        return 0;
    }

    void Entity::SetVirtualWorld(uint32_t world) {
        if (auto *e = Resolve()) {
            e->SetVirtualWorld(world);
        }
    }

    void Entity::SetVisibleTo(Networking::Replication::NetworkEntity *targetEntity) {
        if (auto *e = Resolve()) {
            CoreModules::GetReplication()->SetStreamTarget(*e, targetEntity ? targetEntity->ownerGUID : MafiaNet::UNASSIGNED_PEER_GUID);
        }
    }

    std::string Entity::ToString() const {
        std::ostringstream ss;
        ss << "Entity{ id: " << _id << " }";
        return ss.str();
    }

    namespace {
        // Property: rotation. Reads as a Quaternion; takes a Quaternion or a Vector3 (euler degrees), which is
        // more than one signature says.
        void ReadRotation(Bind::Call &call) {
            if (const auto *self = call.Self<Entity>()) {
                call.Return(call.Wrap(self->GetRotation()));
            }
        }

        void WriteRotation(Bind::Call &call) {
            auto *self = call.Self<Entity>();
            if (!self || call.ArgCount() < 1) {
                return;
            }

            if (const auto *vec = call.Arg(0).AsBound<Vector3>()) {
                self->SetRotationFromEuler(*vec);
                return;
            }
            if (const auto *quat = call.Arg(0).AsBound<Quaternion>()) {
                self->SetRotationFromQuaternion(*quat);
                return;
            }
            call.ThrowTypeError("rotation must be a Vector3 (euler degrees) or Quaternion");
        }

        // setVisibleTo(player|null): restrict streaming to one player's connection.
        void SetVisibleToPlayer(Bind::Call &call) {
            auto *self = call.Self<Entity>();
            if (!self) {
                return;
            }
            if (call.Arg(0).IsNil()) {
                self->SetVisibleTo(nullptr);
                return;
            }
            const auto *target = call.Arg(0).AsBound<Entity>();
            if (!target) {
                call.ThrowTypeError("setVisibleTo: expected a Player (or null to clear)");
                return;
            }
            auto *targetEntity = target->GetHandle();
            if (!targetEntity || targetEntity->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                call.ThrowError("setVisibleTo: target has no owning connection");
                return;
            }
            self->SetVisibleTo(targetEntity);
        }
    } // namespace

    const Bind::Class<Entity> &Entity::Definition() {
        using Bind::Doc;
        namespace Type = Bind::Type;

        static const auto definition = Bind::Class<Entity>("Entity", "Base handle for a live replicated network entity.")
                                           .Constructor<uint64_t>(Doc("Creates a script wrapper for an existing entity with this ID; it does not spawn an entity.").Param("id", "Network entity identifier."))
                                           .Method("toString", &Entity::ToString, Doc("Formats this entity handle for logging and debugging.").Returns("Text containing the network entity ID."))
                                           .Property("id", &Entity::GetId, "Immutable network entity identifier.")
                                           .Property("virtualWorld", &Entity::GetVirtualWorld, "Current virtual-world identifier.")
                                           .Property("position", &Entity::GetPosition, &Entity::SetPosition, "Authoritative world-space position; assignment forces replicated state.")
                                           .Property("rotation", Type::Custom("Quaternion | Vector3", "Quaternion|Vector3"), &ReadRotation, &WriteRotation, "Authoritative rotation; reads return a quaternion and assignments accept a quaternion or Euler angles in degrees.")
                                           // Property: state. Read-only handle onto this entity's state bag — the bag itself is mutable,
                                           // the binding between entity and bag is not, so there is no setter.
                                           .Property(
                                               "state",
                                               [](const Entity &entity) {
                                                   return StateBag(entity.GetId());
                                               },
                                               "Arbitrary key/value state carried by this entity. The server writes it and every client that can see the entity receives it; see StateBag.")
                                           // Replication placement is the server's to decide; a client writes only its own replica.
                                           .Method("setVirtualWorld", &Entity::SetVirtualWorld, Doc("Moves this entity into another virtual world.").Param("world", "Virtual-world identifier used to partition replication and visibility."), Bind::Side::Server)
                                           .Method("setVisibleTo", &SetVisibleToPlayer,
                                               Doc("Restricts replication of this entity to one owning connection while preserving normal range and visibility checks.")
                                                   .Param("player", Type::Custom("Entity | null", "Entity?"), "Player-owned entity whose connection should exclusively receive this entity, or null to clear the restriction."),
                                               Bind::Side::Server)
                                           // Crosses between runtimes as its network id.
                                           .Transfer(&Entity::GetId);
        return definition;
    }

    v8pp::class_<Entity> &Entity::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            // What `rotation` reads as.
            Quaternion::GetClass(isolate);
            auto cls = Definition().MaterialiseV8(isolate, GetScriptingCatalog(isolate), ScriptingSide(isolate));
            // StateBag is reached only through `entity.state`, which gives one from the first read on, and
            // is built here so that it is in the generated declarations as well, which are exported long
            // before that read. It is deliberately not put on the global: there is no bag without an
            // entity, so there is nothing for a script to construct.
            StateBag::GetClass(isolate);
            return cls;
        });
    }

    void Entity::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Entity> &cls = GetClass(isolate);
        auto ctx                  = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Entity"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

    void Entity::RegisterLua(LuaRuntime &runtime) {
        const Bind::Side side = runtime.Options().side;
        // What the properties of an entity read as, whether or not the state has them as globals.
        Vector3::Definition().MaterialiseLua(runtime.State(), side);
        Quaternion::Definition().MaterialiseLua(runtime.State(), side);
        StateBag::RegisterLua(runtime);
        Definition().RegisterLuaGlobal(runtime.State(), side);
    }

    Networking::Replication::NetworkEntity *Entity::Resolve() const {
        auto *replication = CoreModules::GetReplication();
        return replication ? replication->GetEntityByNetworkID(_id) : nullptr;
    }

    void Entity::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }
} // namespace Framework::Scripting::Builtins
