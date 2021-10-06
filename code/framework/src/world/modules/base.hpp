/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <glm/ext.hpp>
#include <flecs/flecs.h>
#include <string>

namespace Framework::World::Modules {
    struct Base {
        struct Transform {
            glm::vec3 pos;
            glm::vec3 vel;
            glm::quat rot = glm::identity<glm::quat>();
        };

        struct Frame {
            flecs::string modelName;
            glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);
        };

        Base(flecs::world& world) {
            world.module<Base>();

            world.component<Transform>();
            world.component<Frame>();
        }
    };
}
