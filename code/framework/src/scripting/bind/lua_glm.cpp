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

namespace Framework::Scripting::Bind {
    bool LuaConvert<glm::vec2>::Is(lua_State *L, int index) {
        return LuaConvert<Builtins::Vector2>::Is(L, index);
    }

    glm::vec2 LuaConvert<glm::vec2>::Get(lua_State *L, int index) {
        return LuaConvert<const Builtins::Vector2 &>::Get(L, index).vec();
    }

    void LuaConvert<glm::vec2>::Push(lua_State *L, const glm::vec2 &value) {
        LuaConvert<Builtins::Vector2>::Push(L, Builtins::Vector2(value));
    }

    const char *LuaConvert<glm::vec2>::Expected() {
        return LuaConvert<Builtins::Vector2>::Expected();
    }

    bool LuaConvert<glm::vec3>::Is(lua_State *L, int index) {
        return LuaConvert<Builtins::Vector3>::Is(L, index);
    }

    glm::vec3 LuaConvert<glm::vec3>::Get(lua_State *L, int index) {
        return LuaConvert<const Builtins::Vector3 &>::Get(L, index).vec();
    }

    void LuaConvert<glm::vec3>::Push(lua_State *L, const glm::vec3 &value) {
        LuaConvert<Builtins::Vector3>::Push(L, Builtins::Vector3(value));
    }

    const char *LuaConvert<glm::vec3>::Expected() {
        return LuaConvert<Builtins::Vector3>::Expected();
    }

    bool LuaConvert<glm::vec4>::Is(lua_State *L, int index) {
        return LuaConvert<Builtins::Vector4>::Is(L, index);
    }

    glm::vec4 LuaConvert<glm::vec4>::Get(lua_State *L, int index) {
        return LuaConvert<const Builtins::Vector4 &>::Get(L, index).vec();
    }

    void LuaConvert<glm::vec4>::Push(lua_State *L, const glm::vec4 &value) {
        LuaConvert<Builtins::Vector4>::Push(L, Builtins::Vector4(value));
    }

    const char *LuaConvert<glm::vec4>::Expected() {
        return LuaConvert<Builtins::Vector4>::Expected();
    }

    bool LuaConvert<glm::quat>::Is(lua_State *L, int index) {
        return LuaConvert<Builtins::Quaternion>::Is(L, index);
    }

    glm::quat LuaConvert<glm::quat>::Get(lua_State *L, int index) {
        return LuaConvert<const Builtins::Quaternion &>::Get(L, index).quat();
    }

    void LuaConvert<glm::quat>::Push(lua_State *L, const glm::quat &value) {
        LuaConvert<Builtins::Quaternion>::Push(L, Builtins::Quaternion(value));
    }

    const char *LuaConvert<glm::quat>::Expected() {
        return LuaConvert<Builtins::Quaternion>::Expected();
    }
} // namespace Framework::Scripting::Bind
