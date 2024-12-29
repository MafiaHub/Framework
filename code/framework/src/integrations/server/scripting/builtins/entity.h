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
#include <sol/sol.hpp>

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

        void ValidateEntity() {
            if (!_ent.is_valid() || !_ent.is_alive()) {
                throw std::runtime_error(fmt::format("Entity handle '{}' is invalid!", _ent.id()));
            }

            const auto st = _ent.get<Framework::World::Modules::Base::Streamable>();
            if (!st) {
                throw std::runtime_error(fmt::format("Entity '{}' is protected!", _ent.id()));
            }
        }
      public:
        Entity(flecs::entity ent) {
            _ent = ent;
            ValidateEntity();
        }

        Entity(flecs::entity_t ent) {
            _ent = flecs::entity(CoreModules::GetWorldEngine()->GetWorld()->get_world(), ent);
            ValidateEntity();
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

        Framework::Scripting::Builtins::Vector3 GetPosition() const {
            const auto tr = _ent.get<Framework::World::Modules::Base::Transform>();
            return Framework::Scripting::Builtins::Vector3(tr->pos.x, tr->pos.y, tr->pos.z);
        }

        Framework::Scripting::Builtins::Quaternion GetRotation() const {
            const auto tr = _ent.get<Framework::World::Modules::Base::Transform>();
            return Framework::Scripting::Builtins::Quaternion(tr->rot.w, tr->rot.x, tr->rot.y, tr->rot.z);
        }

        Framework::Scripting::Builtins::Vector3 GetVelocity() const {
            const auto tr = _ent.get<Framework::World::Modules::Base::Transform>();
            return Framework::Scripting::Builtins::Vector3(tr->vel.x, tr->vel.y, tr->vel.z);
        }

        Framework::Scripting::Builtins::Vector3 GetScale() const {
            const auto fr = _ent.get<Framework::World::Modules::Base::Frame>();
            return Framework::Scripting::Builtins::Vector3(fr->scale.x, fr->scale.y, fr->scale.z);
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

        static void Register(sol::state &luaEngine) {
            sol::usertype<Entity> cls = luaEngine.new_usertype<Entity>("Entity", sol::constructors<Entity(uint64_t)>());
            cls["id"] = sol::property([](const Entity& self) { return self.GetID(); });
            cls["name"] = sol::property([](const Entity& self) { return self.GetName(); });
            cls["nickname"] = sol::property([](const Entity& self) { return self.GetNickname(); });

            cls["destroy"] = &Entity::Destroy;
            cls["getAlwaysVisible"] = &Entity::IsAlwaysVisible;
            cls["getModelHash"] = &Entity::GetModelHash;
            cls["getModelName"] = &Entity::GetModelName;
            cls["getPosition"] = &Entity::GetPosition;
            cls["getRotation"] = &Entity::GetRotation;
            cls["getScale"] = &Entity::GetScale;
            cls["getUpdateInterval"] = &Entity::GetUpdateInterval;
            cls["getVelocity"] = &Entity::GetVelocity;
            cls["getVirtualWorld"] = &Entity::GetVirtualWorld;
            cls["getVisible"] = &Entity::IsVisible;
            cls["setAlwaysVisible"] = &Entity::SetAlwaysVisible;
            cls["setModelHash"] = &Entity::SetModelHash;
            cls["setModelName"] = &Entity::SetModelName;
            cls["setPosition"] = &Entity::SetPosition;
            cls["setRotation"] = &Entity::SetRotation;
            cls["setScale"] = &Entity::SetScale;
            cls["setUpdateInterval"] = &Entity::SetUpdateInterval;
            cls["setVelocity"] = &Entity::SetVelocity;
            cls["setVirtualWorld"] = &Entity::SetVirtualWorld;
            cls["setVisible"] = &Entity::SetVisible;
            cls["toString"] = &Entity::ToString;
        }
    };
} // namespace Framework::Integrations::Scripting
