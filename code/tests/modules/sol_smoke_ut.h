/*
 * MafiaHub OSS license
 * Copyright (c) 2026, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "lua_test_helpers.h"

#include "scripting/bind/sol.h"
#include "scripting/lua/lua_runtime.h"

#include <stdexcept>
#include <string>

namespace SolSmokeTest {
    struct Counter final {
        int value = 0;

        int Add(int amount) {
            value += amount;
            return value;
        }

        int GetValue() const {
            return value;
        }

        void SetValue(int next) {
            value = next;
        }
    };

    inline bool g_unwound = false;

    struct UnwindFlag final {
        ~UnwindFlag() {
            g_unwound = true;
        }
    };

    inline Framework::Scripting::LuaRuntimeOptions Options() {
        Framework::Scripting::LuaRuntimeOptions options;
        options.resourceName = "sol_smoke";
        return options;
    }

    // How many globals of the state have a name that starts with "sol.".
    inline int CountSolGlobals(lua_State *L) {
        int count = 0;
        lua_pushglobaltable(L);
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING && std::string(lua_tostring(L, -2)).rfind("sol.", 0) == 0) {
                ++count;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
        return count;
    }
} // namespace SolSmokeTest

MODULE(sol_smoke, {
    using namespace Framework::Scripting;

    IT("builds against Lua 5.5 compiled as C++", {
        EQUALS(SOL_LUA_VERSION, 505);
        EQUALS(LUA_VERSION_NUM, 505);
    });

    IT("views a state a LuaRuntime made without touching its panic handler or its globals", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();

        const lua_CFunction panic = lua_atpanic(L, nullptr);
        lua_atpanic(L, panic);

        sol::state_view lua(L);
        sol::stack::push(L, 42);
        EQUALS(sol::stack::check<int>(L, -1), true);
        EQUALS(sol::stack::pop<int>(L), 42);
        EQUALS(lua_gettop(L), 0);

        const lua_CFunction after = lua_atpanic(L, nullptr);
        lua_atpanic(L, after);
        EQUALS(after == panic, true);
        EQUALS(SolSmokeTest::CountSolGlobals(L), 0);
        // The sandbox is as it was: sol opened no library.
        EQUALS(LuaTest::Eval(L, "io == nil and debug == nil and dofile == nil"), "true");
    });

    IT("binds a usertype with a method and a property", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();
        sol::state_view lua(L);

        lua.new_usertype<SolSmokeTest::Counter>("Counter", "add", &SolSmokeTest::Counter::Add, "value", sol::property(&SolSmokeTest::Counter::GetValue, &SolSmokeTest::Counter::SetValue));

        EQUALS(LuaTest::Eval(L, "c = Counter.new(); c.value = 40; return c:add(2)"), "42");
        EQUALS(LuaTest::Eval(L, "c.value"), "42");
    });

    // Why Bind::Class does not materialise a class as a sol usertype: sol keeps what a usertype is made of in a
    // userdata that only a global of the state holds, and finds it again through that global, unchecked. A script
    // reads, replaces and drops its globals.
    IT("keeps the storage of a usertype in a global a script reaches", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();
        sol::state_view lua(L);

        EQUALS(SolSmokeTest::CountSolGlobals(L), 0);
        lua.new_usertype<SolSmokeTest::Counter>("Counter", "add", &SolSmokeTest::Counter::Add);
        EQUALS(SolSmokeTest::CountSolGlobals(L), 1);
        EQUALS(LuaTest::Eval(L, "for name, value in pairs(_G) do if name:sub(1, 4) == 'sol.' then return type(value) end end"), "userdata");
    });

    IT("turns a C++ exception from a bound function into a Lua error and leaves the state usable", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();
        sol::state_view lua(L);

        lua.set_function("fail", []() -> int {
            throw std::runtime_error("boom from C++");
        });

        EQUALS(LuaTest::Eval(L, "fail()"), "<error> boom from C++");
        EQUALS(LuaTest::Eval(L, "select(2, pcall(fail))"), "boom from C++");
        EQUALS(LuaTest::Eval(L, "1 + 1"), "2");
        EQUALS(lua_gettop(L), 0);
    });

    IT("runs destructors when a Lua error unwinds through a bound function", {
        LuaRuntime runtime(SolSmokeTest::Options());
        lua_State *L = runtime.State();
        sol::state_view lua(L);

        lua.set_function("raise", [](lua_State *state) -> int {
            SolSmokeTest::UnwindFlag flag;
            return luaL_error(state, "raised in Lua");
        });
        // With the safeties on sol::function is a protected call; the unsafe one is lua_call.
        lua.set_function("callback", [](sol::unsafe_function function) {
            SolSmokeTest::UnwindFlag flag;
            function();
        });

        SolSmokeTest::g_unwound = false;
        EQUALS(LuaTest::Eval(L, "raise()"), "<error> eval:1: raised in Lua");
        EQUALS(SolSmokeTest::g_unwound, true);

        // The error of a script, raised below a C++ frame that called back into Lua unprotected.
        SolSmokeTest::g_unwound = false;
        EQUALS(LuaTest::Eval(L, "callback(function() error('from script', 0) end)"), "<error> from script");
        EQUALS(SolSmokeTest::g_unwound, true);
        EQUALS(LuaTest::Eval(L, "1 + 1"), "2");
    });
});
