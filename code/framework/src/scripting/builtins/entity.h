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

#include <core_modules.h>
#include <networking/replication/network_entity.h>
#include <world/engine.h>

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Framework::Scripting::Builtins {
    // Base scripting handle for a replicated entity, reusable across mods. Wraps the entity's
    // NetworkID and resolves the live NetworkEntity on demand through the world engine, so a stale JS
    // handle to a destroyed entity resolves to null instead of dangling. Exposes the common transform
    // (position/rotation); writing it goes through NetworkEntity::ForceState, so the server's value is
    // authoritative even over an entity a client owns. Mods derive their own handles (player, vehicle)
    // via v8pp inherit<Entity>() and add their game-specific properties.
    //
    // Header-only (like property.h) so it compiles against whichever V8 the including target links
    // (libnode on the server, standalone V8 on the client).
    class Entity {
      public:
        Entity(uint64_t networkId): _id(networkId) {
            if (!Resolve()) {
                throw std::runtime_error(fmt::format("Entity handle '{}' is not valid!", networkId));
            }
        }
        virtual ~Entity() = default;

        uint64_t GetId() const {
            return _id;
        }

        Networking::Replication::NetworkEntity *GetHandle() const {
            return Resolve();
        }

        Vector3 GetPosition() const {
            if (auto *e = Resolve()) {
                return Vector3(e->position.x, e->position.y, e->position.z);
            }
            return Vector3(0, 0, 0);
        }

        void SetPosition(const Vector3 &pos) {
            if (auto *e = Resolve()) {
                e->position = pos.vec();
                e->ForceState();
            }
        }

        Vector3 GetRotation() const {
            if (auto *e = Resolve()) {
                glm::vec3 euler = glm::eulerAngles(e->rotation);
                return Vector3(glm::degrees(euler.x), glm::degrees(euler.y), glm::degrees(euler.z));
            }
            return Vector3(0, 0, 0);
        }

        void SetRotationFromEuler(const Vector3 &rot) {
            if (auto *e = Resolve()) {
                glm::vec3 radians(glm::radians(rot.vec().x), glm::radians(rot.vec().y), glm::radians(rot.vec().z));
                e->rotation = glm::quat(radians);
                e->ForceState();
            }
        }

        void SetRotationFromQuaternion(const Quaternion &quat) {
            if (auto *e = Resolve()) {
                e->rotation = quat.quat();
                e->ForceState();
            }
        }

        std::string GetModelName() const {
            if (auto *e = Resolve()) {
                return e->modelName;
            }
            return "";
        }

        virtual std::string ToString() const {
            std::ostringstream ss;
            ss << "Entity{ id: " << _id << " }";
            return ss.str();
        }

        static v8pp::class_<Entity> &GetClass(v8::Isolate *isolate) {
            auto it = _classes.find(isolate);
            if (it != _classes.end()) {
                return *it->second;
            }

            auto &cls = _classes[isolate];
            cls = std::make_unique<v8pp::class_<Entity>>(isolate);
            cls->auto_wrap_objects(true);
            cls->ctor<uint64_t>()
                .function("toString", &Entity::ToString);

            auto protoTemplate = cls->class_function_template()->PrototypeTemplate();

            // Read-only property: id
            protoTemplate->SetNativeDataProperty(
                v8pp::to_v8(isolate, "id").As<v8::Name>(),
                [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Entity>::unwrap_object(info.GetIsolate(), info.This());
                    if (self) info.GetReturnValue().Set(static_cast<double>(self->GetId()));
                });

            // Read-only property: modelName
            protoTemplate->SetNativeDataProperty(
                v8pp::to_v8(isolate, "modelName").As<v8::Name>(),
                [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Entity>::unwrap_object(info.GetIsolate(), info.This());
                    if (self) info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), self->GetModelName()));
                });

            // Property: position (Vector3). Accessor pair (not SetNativeDataProperty) so the setter
            // fires when a script assigns through an instance whose accessor lives on the prototype.
            {
                auto positionGetter = v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Entity>::unwrap_object(info.GetIsolate(), info.This());
                    if (self) {
                        auto pos     = self->GetPosition();
                        auto &vecCls = Vector3::GetClass(info.GetIsolate());
                        info.GetReturnValue().Set(vecCls.import_external(info.GetIsolate(), new Vector3(pos)));
                    }
                });
                auto positionSetter = v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Entity>::unwrap_object(info.GetIsolate(), info.This());
                    if (!self || info.Length() < 1) return;
                    auto *vec = v8pp::class_<Vector3>::unwrap_object(info.GetIsolate(), info[0]);
                    if (vec) self->SetPosition(*vec);
                });
                protoTemplate->SetAccessorProperty(v8pp::to_v8(isolate, "position").As<v8::Name>(), positionGetter, positionSetter);
            }

            // Property: rotation (accepts both Vector3 euler degrees and Quaternion).
            {
                auto rotationGetter = v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Entity>::unwrap_object(info.GetIsolate(), info.This());
                    if (self) {
                        auto rot     = self->GetRotation();
                        auto &vecCls = Vector3::GetClass(info.GetIsolate());
                        info.GetReturnValue().Set(vecCls.import_external(info.GetIsolate(), new Vector3(rot)));
                    }
                });
                auto rotationSetter = v8::FunctionTemplate::New(isolate, [](const v8::FunctionCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Entity>::unwrap_object(info.GetIsolate(), info.This());
                    if (!self || info.Length() < 1) return;

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
                    info.GetIsolate()->ThrowException(v8::Exception::TypeError(
                        v8pp::to_v8(info.GetIsolate(), "rotation must be a Vector3 (euler degrees) or Quaternion")));
                });
                protoTemplate->SetAccessorProperty(v8pp::to_v8(isolate, "rotation").As<v8::Name>(), rotationGetter, rotationSetter);
            }

            return *cls;
        }

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
            v8pp::class_<Entity> &cls = GetClass(isolate);
            auto ctx                  = isolate->GetCurrentContext();
            global->Set(ctx, v8pp::to_v8(isolate, "Entity"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
        }

      protected:
        Networking::Replication::NetworkEntity *Resolve() const {
            auto *world = CoreModules::GetWorldEngine();
            return world ? world->GetEntityByNetworkID(_id) : nullptr;
        }

        uint64_t _id = 0;
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Entity>>> _classes;
    };
} // namespace Framework::Scripting::Builtins
