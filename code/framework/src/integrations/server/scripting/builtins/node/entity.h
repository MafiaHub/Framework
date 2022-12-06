/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/engines/resource.h"
#include "scripting/keys.h"

#include "scripting/engines/node/builtins/quaternion.h"
#include "scripting/engines/node/builtins/vector_3.h"

#include <glm/glm.hpp>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>

#include "world/engine.h"
#include "world/game_rpc/set_frame.h"
#include "world/game_rpc/set_transform.h"

#include <iomanip>
#include <list>
#include <sstream>

namespace Framework::Integrations::Scripting {
    class Entity {
      protected:
        flecs::entity _ent {};

      public:
        Entity(flecs::entity_t ent) {
            _ent = flecs::entity(Framework::World::Engine::_worldRef->get_world(), ent);
        }

        flecs::entity_t GetID() const {
            return _ent.id();
        }

        std::string GetName() const {
            return _ent.name().c_str();
        }

        virtual std::string ToString() const {
            std::ostringstream ss;
            ss << "Entity{ id: " << _ent.id() << " }";
            return ss.str();
        }

        void SetPosition(Framework::Scripting::Engines::Node::Builtins::Vector3 v3) {
            auto tr = _ent.get_mut<Framework::World::Modules::Base::Transform>();
            tr->pos = glm::vec3(v3.GetX(), v3.GetY(), v3.GetZ());
            FW_SEND_COMPONENT_GAME_RPC(Framework::World::RPC::SetTransform, _ent, *tr);
        }

        void SetRotation(Framework::Scripting::Engines::Node::Builtins::Quaternion q) {
            auto tr = _ent.get_mut<Framework::World::Modules::Base::Transform>();
            tr->rot = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
            FW_SEND_COMPONENT_GAME_RPC(Framework::World::RPC::SetTransform, _ent, *tr);
        }

        void SetVelocity(Framework::Scripting::Engines::Node::Builtins::Vector3 v3) {
            auto tr = _ent.get_mut<Framework::World::Modules::Base::Transform>();
            tr->vel = glm::vec3(v3.GetX(), v3.GetY(), v3.GetZ());
            FW_SEND_COMPONENT_GAME_RPC(Framework::World::RPC::SetTransform, _ent, *tr);
        }

        void SetScale(Framework::Scripting::Engines::Node::Builtins::Vector3 v3) {
            auto fr   = _ent.get_mut<Framework::World::Modules::Base::Frame>();
            fr->scale = glm::vec3(v3.GetX(), v3.GetY(), v3.GetZ());
            FW_SEND_COMPONENT_GAME_RPC(Framework::World::RPC::SetFrame, _ent, *fr);
        }

        void SetModelName(std::string name) {
            auto fr       = _ent.get_mut<Framework::World::Modules::Base::Frame>();
            fr->modelName = name;
            FW_SEND_COMPONENT_GAME_RPC(Framework::World::RPC::SetFrame, _ent, *fr);
        }

        void SetModelHash(uint64_t hash) {
            auto fr       = _ent.get_mut<Framework::World::Modules::Base::Frame>();
            fr->modelHash = hash;
            FW_SEND_COMPONENT_GAME_RPC(Framework::World::RPC::SetFrame, _ent, *fr);
        }

        Framework::Scripting::Engines::Node::Builtins::Vector3 GetPosition() const {
            const auto tr = _ent.get<Framework::World::Modules::Base::Transform>();
            return Framework::Scripting::Engines::Node::Builtins::Vector3(tr->pos.x, tr->pos.y, tr->pos.z);
        }

        Framework::Scripting::Engines::Node::Builtins::Quaternion GetRotation() const {
            const auto tr = _ent.get<Framework::World::Modules::Base::Transform>();
            return Framework::Scripting::Engines::Node::Builtins::Quaternion(tr->rot.w, tr->rot.x, tr->rot.y, tr->rot.z);
        }

        Framework::Scripting::Engines::Node::Builtins::Vector3 GetVelocity() const {
            const auto tr = _ent.get<Framework::World::Modules::Base::Transform>();
            return Framework::Scripting::Engines::Node::Builtins::Vector3(tr->vel.x, tr->vel.y, tr->vel.z);
        }

        Framework::Scripting::Engines::Node::Builtins::Vector3 GetScale() const {
            const auto fr = _ent.get<Framework::World::Modules::Base::Frame>();
            return Framework::Scripting::Engines::Node::Builtins::Vector3(fr->scale.x, fr->scale.y, fr->scale.z);
        }

        std::string GetModelName() const {
            const auto fr = _ent.get<Framework::World::Modules::Base::Frame>();
            return fr->modelName;
        }

        uint64_t GetModelHash() const {
            const auto fr = _ent.get<Framework::World::Modules::Base::Frame>();
            return fr->modelHash;
        }

        void SetVisible(bool visible) {
            auto st       = _ent.get_mut<Framework::World::Modules::Base::Streamable>();
            st->isVisible = visible;
        }

        void SetAlwaysVisible(bool visible) {
            auto st           = _ent.get_mut<Framework::World::Modules::Base::Streamable>();
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

        void SetVirtualWorld(int virtualWorld) {
            auto st          = _ent.get_mut<Framework::World::Modules::Base::Streamable>();
            st->virtualWorld = virtualWorld;
        }

        int GetVirtualWorld() const {
            const auto st = _ent.get<Framework::World::Modules::Base::Streamable>();
            return st->virtualWorld;
        }

        void SetUpdateInterval(double interval) {
            auto st            = _ent.get_mut<Framework::World::Modules::Base::Streamable>();
            st->updateInterval = interval;
        }

        double GetUpdateInterval() const {
            const auto st = _ent.get<Framework::World::Modules::Base::Streamable>();
            return st->updateInterval;
        }

        void Destruct() {
            Framework::World::ServerEngine::RemoveEntity(_ent);
        }

        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            if (!rootModule) {
                return;
            }

            v8pp::class_<Entity> cls(isolate);
            cls.property("id", &Entity::GetID);
            cls.property("name", &Entity::GetName);
            cls.function("toString", &Entity::ToString);
            cls.function("destruct", &Entity::Destruct);

            cls.function("setPosition", &Entity::SetPosition);
            cls.function("setRotation", &Entity::SetRotation);
            cls.function("setVelocity", &Entity::SetVelocity);
            cls.function("setScale", &Entity::SetScale);
            cls.function("setModelName", &Entity::SetModelName);
            cls.function("setModelHash", &Entity::SetModelHash);

            cls.function("getPosition", &Entity::GetPosition);
            cls.function("getRotation", &Entity::GetRotation);
            cls.function("getVelocity", &Entity::GetVelocity);
            cls.function("getModelName", &Entity::GetModelName);
            cls.function("getModelHash", &Entity::GetModelHash);
            cls.function("getScale", &Entity::GetScale);

            cls.function("setVisible", &Entity::SetVisible);
            cls.function("setAlwaysVisible", &Entity::SetAlwaysVisible);
            cls.function("setVirtualWorld", &Entity::SetVirtualWorld);
            cls.function("setUpdateInterval", &Entity::SetUpdateInterval);
            cls.function("getVisible", &Entity::IsVisible);
            cls.function("getAlwaysVisible", &Entity::IsAlwaysVisible);
            cls.function("getVirtualWorld", &Entity::GetVirtualWorld);
            cls.function("getUpdateInterval", &Entity::GetUpdateInterval);

            rootModule->class_("Entity", cls);
        }
    };
} // namespace Framework::Integrations::Scripting
