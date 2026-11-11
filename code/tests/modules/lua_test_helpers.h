/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <cstdlib>
#include <limits>
#include <string>

namespace LuaTest {
    namespace Detail {
        inline void *Allocate(void *, void *block, size_t, size_t newSize) {
            if (newSize == 0) {
                std::free(block);
                return nullptr;
            }
            return std::realloc(block, newSize);
        }

        // Compiles `source` as an expression when it is one, as a chunk otherwise, like the Lua prompt does. Leaves
        // the compiled function on the stack, or the chunk's compile error. Text only: a binary chunk is refused.
        inline bool Compile(lua_State *L, const std::string &source) {
            const std::string expression = "return " + source;
            if (luaL_loadbufferx(L, expression.data(), expression.size(), "=eval", "t") == LUA_OK) {
                return true;
            }
            lua_pop(L, 1);
            return luaL_loadbufferx(L, source.data(), source.size(), "=eval", "t") == LUA_OK;
        }

        // Takes the compiled function, runs it and spells its first result as tostring() would. Protected by the
        // caller, since a __tostring metamethod can raise.
        inline int RunAndSpell(lua_State *L) {
            lua_call(L, 0, 1);
            luaL_tolstring(L, -1, nullptr);
            return 1;
        }

        inline std::string ErrorText(lua_State *L) {
            if (const char *message = lua_tostring(L, -1)) {
                return message;
            }
            return std::string("(error object is a ") + luaL_typename(L, -1) + " value)";
        }
    } // namespace Detail

    // A state with the base, coroutine, table, string, math and utf8 libraries, closed when the test leaves. The
    // seed is fixed so a run is reproducible.
    struct State final {
        lua_State *L;

        State(): L(lua_newstate(Detail::Allocate, nullptr, 0)) {
            luaL_openselectedlibs(L, LUA_GLIBK | LUA_COLIBK | LUA_TABLIBK | LUA_STRLIBK | LUA_MATHLIBK | LUA_UTF8LIBK, 0);
        }

        ~State() {
            lua_close(L);
        }

        State(const State &)            = delete;
        State &operator=(const State &) = delete;
    };

    // Runs `source`, an expression or a chunk, in protected mode. Returns the first result as text ("nil", "true",
    // "2", "1.5", the string itself), or "<error> " followed by the message. A message from error() or luaL_error
    // starts with the position of the Lua code that raised it: "<error> eval:1: boom".
    inline std::string Eval(lua_State *L, const std::string &source) {
        const int top = lua_gettop(L);
        std::string text;
        lua_pushcfunction(L, Detail::RunAndSpell);
        if (Detail::Compile(L, source) && lua_pcall(L, 1, 1, 0) == LUA_OK) {
            size_t length      = 0;
            const char *result = lua_tolstring(L, -1, &length);
            text.assign(result, length);
        }
        else {
            text = "<error> " + Detail::ErrorText(L);
        }
        lua_settop(L, top);
        return text;
    }

    // The first result of `source` as a number, NaN when it failed or is not a number.
    inline double EvalNumber(lua_State *L, const std::string &source) {
        const int top = lua_gettop(L);
        double number = std::numeric_limits<double>::quiet_NaN();
        if (Detail::Compile(L, source) && lua_pcall(L, 0, 1, 0) == LUA_OK && lua_type(L, -1) == LUA_TNUMBER) {
            number = lua_tonumber(L, -1);
        }
        lua_settop(L, top);
        return number;
    }
} // namespace LuaTest
