/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "lua_glm.h"

#include "../builtins/quaternion.h"
#include "../builtins/vector2.h"
#include "../builtins/vector3.h"
#include "../builtins/vector4.h"

namespace Framework::Scripting::Bind::Detail {
    bool LuaIsGlm(sol::types<glm::vec2>, lua_State *L, int index) {
        return LuaConvert<Builtins::Vector2>::Is(L, index);
    }

    bool LuaIsGlm(sol::types<glm::vec3>, lua_State *L, int index) {
        return LuaConvert<Builtins::Vector3>::Is(L, index);
    }

    bool LuaIsGlm(sol::types<glm::vec4>, lua_State *L, int index) {
        return LuaConvert<Builtins::Vector4>::Is(L, index);
    }

    bool LuaIsGlm(sol::types<glm::quat>, lua_State *L, int index) {
        return LuaConvert<Builtins::Quaternion>::Is(L, index);
    }
} // namespace Framework::Scripting::Bind::Detail

namespace glm {
    using namespace Framework::Scripting;

    vec2 sol_lua_get(sol::types<vec2>, lua_State *L, int index, sol::stack::record &tracking) {
        tracking.use(1);
        return Bind::LuaConvert<const Builtins::Vector2 &>::Get(L, index).vec();
    }

    vec3 sol_lua_get(sol::types<vec3>, lua_State *L, int index, sol::stack::record &tracking) {
        tracking.use(1);
        return Bind::LuaConvert<const Builtins::Vector3 &>::Get(L, index).vec();
    }

    vec4 sol_lua_get(sol::types<vec4>, lua_State *L, int index, sol::stack::record &tracking) {
        tracking.use(1);
        return Bind::LuaConvert<const Builtins::Vector4 &>::Get(L, index).vec();
    }

    quat sol_lua_get(sol::types<quat>, lua_State *L, int index, sol::stack::record &tracking) {
        tracking.use(1);
        return Bind::LuaConvert<const Builtins::Quaternion &>::Get(L, index).quat();
    }

    int sol_lua_push(lua_State *L, const vec2 &value) {
        Bind::LuaConvert<Builtins::Vector2>::Push(L, Builtins::Vector2(value));
        return 1;
    }

    int sol_lua_push(lua_State *L, const vec3 &value) {
        Bind::LuaConvert<Builtins::Vector3>::Push(L, Builtins::Vector3(value));
        return 1;
    }

    int sol_lua_push(lua_State *L, const vec4 &value) {
        Bind::LuaConvert<Builtins::Vector4>::Push(L, Builtins::Vector4(value));
        return 1;
    }

    int sol_lua_push(lua_State *L, const quat &value) {
        Bind::LuaConvert<Builtins::Quaternion>::Push(L, Builtins::Quaternion(value));
        return 1;
    }
} // namespace glm

namespace Framework::Scripting::Bind {
    const char *LuaConvert<glm::vec2>::Expected() {
        return LuaConvert<Builtins::Vector2>::Expected();
    }

    const char *LuaConvert<glm::vec3>::Expected() {
        return LuaConvert<Builtins::Vector3>::Expected();
    }

    const char *LuaConvert<glm::vec4>::Expected() {
        return LuaConvert<Builtins::Vector4>::Expected();
    }

    const char *LuaConvert<glm::quat>::Expected() {
        return LuaConvert<Builtins::Quaternion>::Expected();
    }
} // namespace Framework::Scripting::Bind
