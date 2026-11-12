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
// sol2 is taught the four types through its customization points, which it finds in the namespace of glm, and
// LuaConvert reads and pushes them through sol::stack like any type sol knows.
//
// The functions are only declared here, so that this file needs no header of the wrapper classes, which themselves
// include the binding layer. They are defined in lua_glm.cpp. Pushing raises a Lua error in a state the wrapper
// class is not registered in.
namespace Framework::Scripting::Bind::Detail {
    bool LuaIsGlm(sol::types<glm::vec2>, lua_State *L, int index);
    bool LuaIsGlm(sol::types<glm::vec3>, lua_State *L, int index);
    bool LuaIsGlm(sol::types<glm::vec4>, lua_State *L, int index);
    bool LuaIsGlm(sol::types<glm::quat>, lua_State *L, int index);
} // namespace Framework::Scripting::Bind::Detail

namespace glm {
    template <typename T, typename Handler>
        requires requires(lua_State *L) { Framework::Scripting::Bind::Detail::LuaIsGlm(sol::types<T>(), L, 0); }
    bool sol_lua_check(sol::types<T> type, lua_State *L, int index, Handler &&handler, sol::stack::record &tracking) {
        tracking.use(1);
        if (Framework::Scripting::Bind::Detail::LuaIsGlm(type, L, index)) {
            return true;
        }
        handler(L, index, sol::type::userdata, sol::type_of(L, index), "");
        return false;
    }

    vec2 sol_lua_get(sol::types<vec2>, lua_State *L, int index, sol::stack::record &tracking);
    vec3 sol_lua_get(sol::types<vec3>, lua_State *L, int index, sol::stack::record &tracking);
    vec4 sol_lua_get(sol::types<vec4>, lua_State *L, int index, sol::stack::record &tracking);
    quat sol_lua_get(sol::types<quat>, lua_State *L, int index, sol::stack::record &tracking);

    int sol_lua_push(lua_State *L, const vec2 &value);
    int sol_lua_push(lua_State *L, const vec3 &value);
    int sol_lua_push(lua_State *L, const vec4 &value);
    int sol_lua_push(lua_State *L, const quat &value);
} // namespace glm

namespace Framework::Scripting::Bind {
    template <>
    struct LuaConvert<glm::vec2> final: Detail::SolConvert<glm::vec2> {
        static const char *Expected();
    };

    template <>
    struct LuaConvert<glm::vec3> final: Detail::SolConvert<glm::vec3> {
        static const char *Expected();
    };

    template <>
    struct LuaConvert<glm::vec4> final: Detail::SolConvert<glm::vec4> {
        static const char *Expected();
    };

    template <>
    struct LuaConvert<glm::quat> final: Detail::SolConvert<glm::quat> {
        static const char *Expected();
    };
} // namespace Framework::Scripting::Bind
