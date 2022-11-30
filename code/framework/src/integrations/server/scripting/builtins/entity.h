/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "scripting/engines/resource.h"
#include "scripting/keys.h"

#include <glm/glm.hpp>
#include <v8pp/class.hpp>
#include <v8pp/module.hpp>

#include "world/engine.h"

#include <iomanip>
#include <list>
#include <sstream>

namespace Framework::Integrations::Scripting {
    class Entity {
      private:
        flecs::entity _ent {};

      public:
        Entity(flecs::entity_t ent) {
            _ent = flecs::entity(Framework::World::Engine::_worldRef->get_world(), ent);
        }

        flecs::entity_t GetID() const {
            return _ent.id();
        }

        std::string ToString() const {
            std::ostringstream ss;
            ss << "Entity{ id: " << _ent.id() << " }";
            return ss.str();
        }
    };

    static void EntityRegister(v8::Isolate *isolate, v8pp::module *rootModule) {
        if (!rootModule) {
            return;
        }

        v8pp::class_<Entity> cls(isolate);
        cls.ctor<flecs::entity_t>();
        cls.property("id", &Entity::GetID);
        cls.function("toString", &Entity::ToString);

        rootModule->class_("Entity", cls);
    }
} // namespace Framework::Integrations::Scripting
