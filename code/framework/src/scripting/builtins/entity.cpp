/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "entity.h"
#include "../engine_helpers.h"
#include "../api/binding.h"
#include "../api/native_value_types.h"
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

    const Api::ClassBinding<Entity> &Entity::Binding() {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        using v8pp::metadata::property_docs;

        static const Api::ClassBinding<Entity> &binding = *[] {
            auto *b = new Api::ClassBinding<Entity>("Entity", "Base handle for a live replicated network entity.");
            b->Constructor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing entity with this ID; it does not spawn an entity."))
                .Method<&Entity::ToString>("toString", docs("string", {}, "Formats this entity handle for logging and debugging.", "Text containing the network entity ID."))
                // The C# handle carries its id itself, so a stale handle still knows which entity it named.
                .Property<&Entity::GetId>("id", property_docs("number", "Immutable network entity identifier."), Api::Side::Shared, Api::Target::JavaScript)
                .Property<&Entity::GetVirtualWorld>("virtualWorld", property_docs("number", "Current virtual-world identifier."))
                .Property<&Entity::GetPosition, &Entity::SetPosition>("position", property_docs("Vector3", "Authoritative world-space position; assignment forces replicated state."));

            // Property: rotation. Getter returns a Quaternion; setter accepts a Quaternion or a Vector3 (euler degrees).
            // A typed language gets the two shapes as a property and a method instead of one union-typed setter.
            b->JavaScript([](v8::Isolate *isolate, v8pp::class_<Entity> &cls) {
                auto rotationGetter = v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Entity>::unwrap_object(info.GetIsolate(), info.This());
                    if (self) {
                        auto &quatCls = Quaternion::GetClass(info.GetIsolate());
                        info.GetReturnValue().Set(quatCls.import_external(info.GetIsolate(), new Quaternion(self->GetRotation())));
                    }
                });
                auto rotationSetter = v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Entity>::unwrap_object(info.GetIsolate(), info.This());
                    if (!self || info.Length() < 1)
                        return;

                    auto *vec = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info[0]);
                    if (vec) {
                        self->SetRotationFromEuler(*vec);
                        return;
                    }
                    auto *quat = v8pp::class_<Quaternion>::unwrap_object(info.GetIsolate(), info[0]);
                    if (quat) {
                        self->SetRotationFromQuaternion(*quat);
                        return;
                    }
                    ThrowTypeError(info.GetIsolate(), "rotation must be a Vector3 (euler degrees) or Quaternion");
                });
                cls.accessor_property("rotation", rotationGetter, rotationSetter, property_docs("Quaternion | Vector3", "Authoritative rotation; reads return a quaternion and assignments accept a quaternion or Euler angles in degrees."));
            });
            b->Property<&Entity::GetRotation, &Entity::SetRotationFromQuaternion>("rotation", property_docs("Quaternion", "Authoritative rotation; assignment forces replicated state."), Api::Side::Shared, Api::Target::Native)
                .Method<&Entity::SetRotationFromEuler>("setRotationFromEuler", docs("void", {param("degrees", "Vector3", false, "Euler angles in degrees.")}, "Sets the authoritative rotation from Euler angles in degrees."), Api::Side::Shared, Api::Target::Native);

            // Property: state. Read-only handle onto this entity's state bag — the bag itself is mutable,
            // the binding between entity and bag is not, so there is no setter.
            b->JavaScript([](v8::Isolate *isolate, v8pp::class_<Entity> &cls) {
                auto stateGetter = v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Entity>::unwrap_object(info.GetIsolate(), info.This());
                    if (self) {
                        info.GetReturnValue().Set(StateBag::NewInstance(info.GetIsolate(), self->GetId()));
                    }
                });
                cls.accessor_property("state", stateGetter, v8::Local<v8::FunctionTemplate>(),
                    property_docs("StateBag", "Arbitrary key/value state carried by this entity. The server writes it and every client that can see the entity receives it; see StateBag."));
            });

            // Replication placement is the server's to decide; a client writes only its own replica.
            b->Method<&Entity::SetVirtualWorld>("setVirtualWorld", docs("void", {param("world", "number", false, "Virtual-world identifier used to partition replication and visibility.")}, "Moves this entity into another virtual world."), Api::Side::Server);

            // setVisibleTo(player|null): restrict streaming to one player's connection.
            b->JavaScript(
                [](v8::Isolate *, v8pp::class_<Entity> &cls) {
                    cls.prototype_function(
                        "setVisibleTo",
                        [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                            auto *isolate = info.GetIsolate();
                            auto *self    = v8pp::class_<Entity>::unwrap_object(isolate, info.This());
                            if (!self) {
                                return;
                            }
                            if (info.Length() < 1 || info[0]->IsNullOrUndefined()) {
                                self->SetVisibleTo(nullptr);
                                return;
                            }
                            auto *target = v8pp::class_<Entity>::unwrap_object(isolate, info[0]);
                            if (!target) {
                                ThrowTypeError(isolate, "setVisibleTo: expected a Player (or null to clear)");
                                return;
                            }
                            auto *targetEntity = target->Resolve();
                            if (!targetEntity || targetEntity->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                                isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "setVisibleTo: target has no owning connection")));
                                return;
                            }
                            self->SetVisibleTo(targetEntity);
                        },
                        docs("void", {param("player", "Entity | null", false, "Player-owned entity whose connection should exclusively receive this entity, or null to clear the restriction.")},
                            "Restricts replication of this entity to one owning connection while preserving normal range and visibility checks."));
                },
                Api::Side::Server);
            return b;
        }();
        return binding;
    }

    v8pp::class_<Entity> &Entity::GetClass(v8::Isolate *isolate) {
        return _classes.Get(isolate, [isolate]() {
            return Binding().Build(isolate);
        });
    }

    void Entity::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Entity> &cls = GetClass(isolate);
        auto ctx                  = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Entity"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
        // StateBag is reached only through `entity.state`, so its class would otherwise not exist
        // until the first read -- long after the metadata export runs, leaving it out of the
        // generated declarations. Build it here instead. It is deliberately not put on the global:
        // there is no bag without an entity, so there is nothing for a script to construct.
        StateBag::GetClass(isolate);
    }

    Networking::Replication::NetworkEntity *Entity::Resolve() const {
        auto *replication = CoreModules::GetReplication();
        return replication ? replication->GetEntityByNetworkID(_id) : nullptr;
    }

    void Entity::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.Unregister(isolate);
    }
} // namespace Framework::Scripting::Builtins
