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

namespace LuaSmokeTest {
    static void *Allocate(void *, void *block, size_t, size_t newSize) {
        if (newSize == 0) {
            std::free(block);
            return nullptr;
        }
        return std::realloc(block, newSize);
    }

    // A state on the malloc-backed allocator with the base library, closed when the test leaves.
    struct State {
        lua_State *L = lua_newstate(Allocate, nullptr, 0);

        State() {
            luaL_openselectedlibs(L, LUA_GLIBK, 0);
        }

        ~State() {
            lua_close(L);
        }
    };

    static bool g_unwound = false;

    struct UnwindFlag {
        ~UnwindFlag() {
            g_unwound = true;
        }
    };

    // Raises with a live local: its destructor only runs if the error leaves this frame as a C++ exception.
    static int RaiseWithLocal(lua_State *L) {
        UnwindFlag flag;
        return luaL_error(L, "raised");
    }
} // namespace LuaSmokeTest

MODULE(lua_smoke, {
    IT("is Lua 5.5", { EQUALS(LUA_VERSION_NUM, 505); });

    IT("runs a text chunk in a state on a malloc-backed allocator", {
        LuaSmokeTest::State state;
        lua_State *L = state.L;

        const char chunk[] = "return 1 + 1";
        EQUALS(luaL_loadbufferx(L, chunk, sizeof(chunk) - 1, "=lua_smoke", "t"), LUA_OK);
        EQUALS(lua_pcall(L, 0, 1, 0), LUA_OK);
        EQUALS(lua_isinteger(L, -1), 1);
        EQUALS(lua_tointeger(L, -1), 2);
    });

    IT("unwinds C++ frames when an error is raised under lua_pcall", {
        LuaSmokeTest::State state;
        lua_State *L = state.L;

        LuaSmokeTest::g_unwound = false;
        lua_pushcfunction(L, LuaSmokeTest::RaiseWithLocal);
        EQUALS(lua_pcall(L, 0, 0, 0), LUA_ERRRUN);
        EQUALS(LuaSmokeTest::g_unwound, true);
        STREQUALS(lua_tostring(L, -1), "raised");
    });
});
