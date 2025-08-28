/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <sol/sol.hpp>
#include <flecs.h>
#include "scripting/builtins/quaternion.h"
#include "scripting/builtins/vector_3.h"

namespace Framework::Integrations::Scripting {
    class Entity {
      protected:
        flecs::entity _ent {};

        void ValidateEntity();
        void EmitEvent(std::string eventName, sol::object payload);
      public:
        Entity(flecs::entity ent);
        Entity(flecs::entity_t ent);

        flecs::entity_t GetID() const {
            return _ent.id();
        }

        flecs::entity GetHandle() const {
            return _ent;
        }

        std::string GetName() const;

        std::string GetNickname() const;
        virtual std::string ToString() const;
        void SetPosition(Framework::Scripting::Builtins::Vector3 v3) const;
        void SetRotation(Framework::Scripting::Builtins::Quaternion q) const;
        void SetVelocity(Framework::Scripting::Builtins::Vector3 v3) const;
        void SetScale(Framework::Scripting::Builtins::Vector3 v3) const;
        void SetModelName(std::string name) const;
        void SetModelHash(uint64_t hash) const;
        Framework::Scripting::Builtins::Vector3 GetPosition() const;
        Framework::Scripting::Builtins::Quaternion GetRotation() const;
        Framework::Scripting::Builtins::Vector3 GetVelocity() const;
        Framework::Scripting::Builtins::Vector3 GetScale() const;
        std::string GetModelName() const;
        uint64_t GetModelHash() const;
        void SetVisible(bool visible) const;
        void SetAlwaysVisible(bool visible) const;
        bool IsVisible() const;
        bool IsAlwaysVisible() const ;
        void SetVirtualWorld(int virtualWorld) const;
        int GetVirtualWorld() const;
        void SetUpdateInterval(double interval) const;
        double GetUpdateInterval() const;
        void Destroy() const;

        bool operator==(const Entity &other) const {
            return this->_ent == other._ent;
        }

        static void Register(sol::state *luaEngine);
    };
} // namespace Framework::Integrations::Scripting
