/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "entity.h"
#include "../scripting_catalog.h"

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
            e->streaming.targetGUID = targetEntity ? targetEntity->ownerGUID : MafiaNet::UNASSIGNED_PEER_GUID;
        }
    }

    std::string Entity::ToString() const {
        std::ostringstream ss;
        ss << "Entity{ id: " << _id << " }";
        return ss.str();
    }

    v8pp::class_<Entity> &Entity::GetClass(v8::Isolate *isolate) {
        auto it = _classes.find(isolate);
        if (it != _classes.end()) {
            return *it->second;
        }

        auto &cls = _classes[isolate];
        cls       = std::make_unique<v8pp::class_<Entity>>(isolate, GetScriptingCatalog(isolate), "Entity", "Base handle for a live replicated network entity.");
        cls->auto_wrap_objects(true);
        cls->ctor<uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing entity with this ID; it does not spawn an entity."))
            .function("toString", &Entity::ToString, v8pp::metadata::docs("string", {}, "Formats this entity handle for logging and debugging.", "Text containing the network entity ID."))
            .property("id", &Entity::GetId, v8pp::metadata::property_docs("number", "Immutable network entity identifier."))
            .property("virtualWorld", &Entity::GetVirtualWorld, v8pp::metadata::property_docs("number", "Current virtual-world identifier."))
            .property("position", &Entity::GetPosition, &Entity::SetPosition, v8pp::metadata::property_docs("Vector3", "Authoritative world-space position; assignment forces replicated state."));

        // Property: rotation. Getter returns a Quaternion; setter accepts a Quaternion or a Vector3 (euler degrees).
        {
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
                info.GetIsolate()->ThrowException(v8::Exception::TypeError(v8pp::to_v8(info.GetIsolate(), "rotation must be a Vector3 (euler degrees) or Quaternion")));
            });
            cls->accessor_property("rotation", rotationGetter, rotationSetter, v8pp::metadata::property_docs("Quaternion | Vector3", "Authoritative rotation; reads return a quaternion and assignments accept a quaternion or Euler angles in degrees."));
        }

        // Replication placement is the server's to decide; a client writes only its own replica.
        if (IsClientScripting(isolate)) {
            return *cls;
        }

        cls->function("setVirtualWorld", &Entity::SetVirtualWorld, v8pp::metadata::docs("void", {v8pp::metadata::param("world", "number", false, "Virtual-world identifier used to partition replication and visibility.")}, "Moves this entity into another virtual world."));

        // setVisibleTo(player|null): restrict streaming to one player's connection.
        cls->prototype_function(
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
                    isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "setVisibleTo: expected a Player (or null to clear)")));
                    return;
                }
                auto *targetEntity = target->Resolve();
                if (!targetEntity || targetEntity->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                    isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, "setVisibleTo: target has no owning connection")));
                    return;
                }
                self->SetVisibleTo(targetEntity);
            },
            v8pp::metadata::docs("void", {v8pp::metadata::param("player", "Entity | null", false, "Player-owned entity whose connection should exclusively receive this entity, or null to clear the restriction.")},
                "Restricts replication of this entity to one owning connection while preserving normal range and visibility checks."));

        return *cls;
    }

    void Entity::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Entity> &cls = GetClass(isolate);
        auto ctx                  = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Entity"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

    Networking::Replication::NetworkEntity *Entity::Resolve() const {
        auto *replication = CoreModules::GetReplication();
        return replication ? replication->GetEntityByNetworkID(_id) : nullptr;
    }

    void Entity::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }
} // namespace Framework::Scripting::Builtins
