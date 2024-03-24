/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/builtins/quaternion.h"
#include "scripting/builtins/vector_3.h"

#include <glm/glm.hpp>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>

#include "networking/network_server.h"

#include "world/engine.h"
#include "world/server.h"

#include "world/game_rpc/set_frame.h"
#include "world/game_rpc/set_transform.h"

#include <iomanip>
#include <list>
#include <sstream>

#include "core_modules.h"

namespace Framework::Integrations::Scripting {
    class Entity {
      protected:
        flecs::entity _ent {};

      public:
        Entity(flecs::entity_t ent) {
            _ent = flecs::entity(CoreModules::GetWorldEngine()->GetWorld()->get_world(), ent);
        }

        static v8::Local<v8::Object> WrapEntity(v8::Isolate *isolate, flecs::entity e) {
            return v8pp::class_<Framework::Integrations::Scripting::Entity>::create_object(isolate, e.id());
        }

        flecs::entity_t GetID() const {
            return _ent.id();
        }

        flecs::entity GetHandle() const {
            return _ent;
        }

        std::string GetName() const {
            return _ent.name().c_str();
        }

        std::string GetNickname() const {
            const auto s = _ent.get<Framework::World::Modules::Base::Streamer>();
            return !!s ? s->nickname : "<unknown>";
        }

        virtual std::string ToString() const {
            std::ostringstream ss;
            ss << "Entity{ id: " << _ent.id() << " }";
            return ss.str();
        }

        void SetPosition(Framework::Scripting::Builtins::Vector3 v3) const {
            const auto tr = _ent.get_mut<Framework::World::Modules::Base::Transform>();
            tr->pos       = glm::vec3(v3.GetX(), v3.GetY(), v3.GetZ());
            tr->IncrementGeneration();
            FW_SEND_SERVER_COMPONENT_GAME_RPC(Framework::World::RPC::SetTransform, _ent, *tr);
            CoreModules::GetWorldEngine()->WakeEntity(_ent);
        }

        void SetRotation(Framework::Scripting::Builtins::Quaternion q) const {
            const auto tr = _ent.get_mut<Framework::World::Modules::Base::Transform>();
            tr->rot       = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
            tr->IncrementGeneration();
            FW_SEND_SERVER_COMPONENT_GAME_RPC(Framework::World::RPC::SetTransform, _ent, *tr);
            CoreModules::GetWorldEngine()->WakeEntity(_ent);
        }

        void SetVelocity(Framework::Scripting::Builtins::Vector3 v3) const {
            const auto tr = _ent.get_mut<Framework::World::Modules::Base::Transform>();
            tr->vel       = glm::vec3(v3.GetX(), v3.GetY(), v3.GetZ());
            tr->IncrementGeneration();
            FW_SEND_SERVER_COMPONENT_GAME_RPC(Framework::World::RPC::SetTransform, _ent, *tr);
            CoreModules::GetWorldEngine()->WakeEntity(_ent);
        }

        void SetScale(Framework::Scripting::Builtins::Vector3 v3) const {
            const auto fr = _ent.get_mut<Framework::World::Modules::Base::Frame>();
            fr->scale     = glm::vec3(v3.GetX(), v3.GetY(), v3.GetZ());
            FW_SEND_SERVER_COMPONENT_GAME_RPC(Framework::World::RPC::SetFrame, _ent, *fr);
        }

        void SetModelName(std::string name) const {
            const auto fr = _ent.get_mut<Framework::World::Modules::Base::Frame>();
            fr->modelName = name;
            FW_SEND_SERVER_COMPONENT_GAME_RPC(Framework::World::RPC::SetFrame, _ent, *fr);
        }

        void SetModelHash(uint64_t hash) const {
            const auto fr = _ent.get_mut<Framework::World::Modules::Base::Frame>();
            fr->modelHash = hash;
            FW_SEND_SERVER_COMPONENT_GAME_RPC(Framework::World::RPC::SetFrame, _ent, *fr);
        }

        v8::Local<v8::Object> GetPosition() const {
            const auto tr = _ent.get<Framework::World::Modules::Base::Transform>();
            return v8pp::class_<Framework::Scripting::Builtins::Vector3>::create_object(v8::Isolate::GetCurrent(), tr->pos.x, tr->pos.y, tr->pos.z);
        }

        v8::Local<v8::Object> GetRotation() const {
            const auto tr = _ent.get<Framework::World::Modules::Base::Transform>();
            return v8pp::class_<Framework::Scripting::Builtins::Quaternion>::create_object(v8::Isolate::GetCurrent(), tr->rot.w, tr->rot.x, tr->rot.y, tr->rot.z);
        }

        v8::Local<v8::Object> GetVelocity() const {
            const auto tr = _ent.get<Framework::World::Modules::Base::Transform>();
            return v8pp::class_<Framework::Scripting::Builtins::Vector3>::create_object(v8::Isolate::GetCurrent(), tr->vel.x, tr->vel.y, tr->vel.z);
        }

        v8::Local<v8::Object> GetScale() const {
            const auto fr = _ent.get<Framework::World::Modules::Base::Frame>();
            return v8pp::class_<Framework::Scripting::Builtins::Vector3>::create_object(v8::Isolate::GetCurrent(), fr->scale.x, fr->scale.y, fr->scale.z);
        }

        std::string GetModelName() const {
            const auto fr = _ent.get<Framework::World::Modules::Base::Frame>();
            return fr->modelName;
        }

        uint64_t GetModelHash() const {
            const auto fr = _ent.get<Framework::World::Modules::Base::Frame>();
            return fr->modelHash;
        }

        void SetVisible(bool visible) const {
            const auto st = _ent.get_mut<Framework::World::Modules::Base::Streamable>();
            st->isVisible = visible;
        }

        void SetAlwaysVisible(bool visible) const {
            const auto st     = _ent.get_mut<Framework::World::Modules::Base::Streamable>();
            st->alwaysVisible = visible;
        }

        bool IsVisible() const {
            const auto st = _ent.get<Framework::World::Modules::Base::Streamable>();
            return st->isVisible;
        }

        bool IsAlwaysVisible() const {
            const auto st = _ent.get<Framework::World::Modules::Base::Streamable>();
            return st->alwaysVisible;
        }

        void SetVirtualWorld(int virtualWorld) const {
            const auto st    = _ent.get_mut<Framework::World::Modules::Base::Streamable>();
            st->virtualWorld = virtualWorld;
        }

        int GetVirtualWorld() const {
            const auto st = _ent.get<Framework::World::Modules::Base::Streamable>();
            return st->virtualWorld;
        }

        void SetUpdateInterval(double interval) const {
            const auto st      = _ent.get_mut<Framework::World::Modules::Base::Streamable>();
            st->updateInterval = interval;
        }

        double GetUpdateInterval() const {
            const auto st = _ent.get<Framework::World::Modules::Base::Streamable>();
            return st->updateInterval;
        }

        void Destroy() const {
            Framework::World::ServerEngine::RemoveEntity(_ent);
        }

        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            if (!rootModule) {
                return;
            }

            v8pp::class_<Entity> cls(isolate);
            cls.property("id", &Entity::GetID);
            cls.property("name", &Entity::GetName);
            cls.property("nickname", &Entity::GetNickname);

            cls.function("destroy", &Entity::Destroy);
            cls.function("getAlwaysVisible", &Entity::IsAlwaysVisible);
            cls.function("getModelHash", &Entity::GetModelHash);
            cls.function("getModelName", &Entity::GetModelName);
            cls.function("getPosition", &Entity::GetPosition);
            cls.function("getRotation", &Entity::GetRotation);
            cls.function("getScale", &Entity::GetScale);
            cls.function("getUpdateInterval", &Entity::GetUpdateInterval);
            cls.function("getVelocity", &Entity::GetVelocity);
            cls.function("getVirtualWorld", &Entity::GetVirtualWorld);
            cls.function("getVisible", &Entity::IsVisible);
            cls.function("setAlwaysVisible", &Entity::SetAlwaysVisible);
            cls.function("setModelHash", &Entity::SetModelHash);
            cls.function("setModelName", &Entity::SetModelName);
            cls.function("setPosition", &Entity::SetPosition);
            cls.function("setRotation", &Entity::SetRotation);
            cls.function("setScale", &Entity::SetScale);
            cls.function("setUpdateInterval", &Entity::SetUpdateInterval);
            cls.function("setVelocity", &Entity::SetVelocity);
            cls.function("setVirtualWorld", &Entity::SetVirtualWorld);
            cls.function("setVisible", &Entity::SetVisible);
            cls.function("toString", &Entity::ToString);

            rootModule->class_("Entity", cls);
        }
    };
} // namespace Framework::Integrations::Scripting
