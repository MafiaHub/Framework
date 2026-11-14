/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_convert.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <lua.h>

// How glm values cross Lua: as the userdata of the builtin wrapper class (Vector2, Vector3, Vector4, Quaternion),
// which is also what a script receives. A table with the components is not a vector, where JavaScript takes any
// object that has them (v8pp_glm.h).
//
// The converters are only declared here, so that this file needs no header of the wrapper classes, which
// themselves include the binding layer. They are defined in lua_glm.cpp. Push raises a Lua error in a state the
// wrapper class is not registered in.
namespace Framework::Scripting::Bind {
    template <>
    struct LuaConvert<glm::vec2> final {
        static bool Is(lua_State *L, int index);
        static glm::vec2 Get(lua_State *L, int index);
        static void Push(lua_State *L, const glm::vec2 &value);
        static const char *Expected();
    };

    template <>
    struct LuaConvert<glm::vec3> final {
        static bool Is(lua_State *L, int index);
        static glm::vec3 Get(lua_State *L, int index);
        static void Push(lua_State *L, const glm::vec3 &value);
        static const char *Expected();
    };

    template <>
    struct LuaConvert<glm::vec4> final {
        static bool Is(lua_State *L, int index);
        static glm::vec4 Get(lua_State *L, int index);
        static void Push(lua_State *L, const glm::vec4 &value);
        static const char *Expected();
    };

    template <>
    struct LuaConvert<glm::quat> final {
        static bool Is(lua_State *L, int index);
        static glm::quat Get(lua_State *L, int index);
        static void Push(lua_State *L, const glm::quat &value);
        static const char *Expected();
    };
} // namespace Framework::Scripting::Bind
